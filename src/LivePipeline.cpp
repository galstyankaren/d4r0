#include "d4r0/LivePipeline.h"
#include "d4r0/GpuRegions.h"
#include "d4r0/OcrRecognizer.h"
#include "d4r0/LocalTranslator.h"
#include "d4r0/RegionScheduler.h"
#include "d4r0/DebugLog.h"
#include "d4r0/TextGrouping.h"
#include <winrt/base.h>
#include <algorithm>
#include <chrono>
#include <cwchar>
#include <deque>
#include <dxgi1_4.h>
#include <psapi.h>
#include <future>
#include <map>
#include <unordered_map>
#include <utility>

namespace d4r0 {
namespace {
constexpr unsigned kTile = GpuRegions::tileSize;
constexpr unsigned kCoreWidth = 960;
constexpr unsigned kCoreHeight = 540;
constexpr unsigned kMarginX = 128;
constexpr unsigned kMarginY = 64;
constexpr std::size_t kMaxTranslationCharacters = 6000;
struct Crop { unsigned x{}, y{}, width{}, height{}, coreX{}, coreY{}, coreWidth{}, coreHeight{}; };
struct PendingRegion {
  TextRegion region; RegionJob owner;
  std::vector<std::pair<std::size_t,std::uint64_t>> dependencies;
  bool attempted{}; bool suppressed{};
};
struct SourceWork { RegionJob job; std::vector<std::size_t> regions; bool finished{}; };
std::uint64_t clockMs() {
  return std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
std::uint64_t fileTimeValue(const FILETIME& value) {
  ULARGE_INTEGER result{}; result.LowPart=value.dwLowDateTime; result.HighPart=value.dwHighDateTime; return result.QuadPart;
}
struct ProcessAccounting { std::uint64_t lastWall{}, lastCpu{}; };
std::wstring processResources(unsigned long pid, ProcessAccounting& accounting) {
  auto sample = [&](HANDLE handle, std::uint64_t& bytes, std::uint64_t& cpu) {
    PROCESS_MEMORY_COUNTERS_EX memory{}; memory.cb=sizeof(memory); if(GetProcessMemoryInfo(handle,reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&memory),sizeof(memory))) bytes += memory.PrivateUsage;
    FILETIME created{},exit{},kernel{},user{}; if(GetProcessTimes(handle,&created,&exit,&kernel,&user)) cpu += fileTimeValue(kernel)+fileTimeValue(user);
  };
  std::uint64_t bytes{}, cpu{}; sample(GetCurrentProcess(),bytes,cpu); HANDLE child=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_VM_READ,FALSE,pid); if(child){sample(child,bytes,cpu);CloseHandle(child);}
  const auto now=clockMs(); double cpuPercent=0; if(accounting.lastWall && now>accounting.lastWall) cpuPercent=double(cpu-accounting.lastCpu)/double((now-accounting.lastWall)*10000)*100.0; accounting={now,cpu};
  return L"RAM " + std::to_wstring(bytes/(1024*1024)) + L"MiB | CPU " + std::to_wstring(int(cpuPercent+0.5)) + L"%";
}
float overlap(const Rect& a, const Rect& b) {
  const float left = std::max(a.x,b.x), top = std::max(a.y,b.y);
  const float right = std::min(a.x+a.width,b.x+b.width), bottom = std::min(a.y+a.height,b.y+b.height);
  if (right <= left || bottom <= top) return 0.0F;
  const float intersection = (right-left)*(bottom-top);
  return intersection/(a.width*a.height+b.width*b.height-intersection);
}
}

LivePipeline::LivePipeline(PipelineSettings settings, WindowsGraphicsCapture& capture, RegionCache& cache,
                           std::function<void(std::wstring)> status)
    : settings_(std::move(settings)), capture_(capture), cache_(cache), status_(std::move(status)),
      worker_([this](std::stop_token stop) { run(stop); }) {}
LivePipeline::~LivePipeline() { stop(); }
void LivePipeline::stop() { worker_.request_stop(); if (worker_.joinable()) worker_.join(); }

void LivePipeline::run(std::stop_token stop) {
  bool apartment = false;
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded); apartment = true;
    status_(L"Loading local OCR and selected translation model...");
    OcrDetector detector(settings_.ocrDetector,settings_.ocrAdapter);
    OcrRecognizer recognizer(settings_.ocrRecognizer,settings_.ocrDictionary,settings_.ocrAdapter);
    auto translator = std::make_unique<LocalTranslator>(settings_,stop);
    status_(L"Translation ready - Ctrl+Shift+Tab toggles, Ctrl+Alt+Q exits");
    debugLog("Local OCR and selected model ready");
    std::unique_ptr<GpuRegions> gpu; Microsoft::WRL::ComPtr<IDXGIAdapter3> adapter;
    RegionScheduler scheduler; unsigned width{}, height{}, columns{}, rows{};
    std::uint64_t frameRevision{}, published{}, dropped{}, ocrCrops{}, modelRequests{}, recognizedLines{}, detectorBoxes{}; ProcessAccounting processAccounting;
    double frameMs{}, changeMs{}, ocrMs{}, translateMs{}; std::deque<double> frameSamples;
    auto lastFrame = std::chrono::steady_clock::time_point{}; CapturedFrame frame;
    auto cropFor = [&](unsigned blockX, unsigned blockY) {
      const unsigned coreX = blockX*kCoreWidth, coreY = blockY*kCoreHeight;
      const unsigned coreWidth = std::min(kCoreWidth,width-coreX), coreHeight = std::min(kCoreHeight,height-coreY);
      const unsigned x = coreX > kMarginX ? coreX-kMarginX : 0, y = coreY > kMarginY ? coreY-kMarginY : 0;
      const unsigned right = std::min(width,coreX+coreWidth+kMarginX), bottom = std::min(height,coreY+coreHeight+kMarginY);
      return Crop{x,y,right-x,bottom-y,coreX,coreY,coreWidth,coreHeight};
    };
    auto observe = [&] {
      auto latest = capture_.latestFrame(); if (latest.revision == frameRevision) return;
      if (!latest.texture) { frameRevision=latest.revision; frame={}; gpu.reset(); adapter.Reset(); width=height=columns=rows=0; scheduler.reset(0); cache_.clear(); return; }
      const auto changeStart = std::chrono::steady_clock::now();
      if (lastFrame != std::chrono::steady_clock::time_point{}) frameMs=std::chrono::duration<double,std::milli>(changeStart-lastFrame).count();
      lastFrame=changeStart; if (frameMs>0) { frameSamples.push_back(frameMs); if(frameSamples.size()>256) frameSamples.pop_front(); }
      D3D11_TEXTURE2D_DESC description{}; latest.texture->GetDesc(&description);
      if (!gpu) {
        Microsoft::WRL::ComPtr<ID3D11Device> device; latest.texture->GetDevice(&device); gpu=std::make_unique<GpuRegions>(device.Get());
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        if (SUCCEEDED(device.As(&dxgiDevice))) { Microsoft::WRL::ComPtr<IDXGIAdapter> baseAdapter; if(SUCCEEDED(dxgiDevice->GetAdapter(&baseAdapter))) baseAdapter.As(&adapter); }
      }
      if (width != description.Width || height != description.Height) { width=description.Width; height=description.Height; columns=(width+kTile-1)/kTile; rows=(height+kTile-1)/kTile; scheduler.reset(columns*rows); cache_.clear(); }
      const auto changed=gpu->compare(latest.texture.Get()); auto scheduled=changed;
      for (const auto& region : cache_.visible()) {
        if (!region.sourceId || region.sourceId>scheduled.size()) continue;
        const auto left=std::min(columns-1,unsigned(std::max(0.0F,region.bounds.x))/kTile), top=std::min(rows-1,unsigned(std::max(0.0F,region.bounds.y))/kTile);
        const auto right=std::min(columns-1,unsigned(std::max(0.0F,region.bounds.x+region.bounds.width-1))/kTile), bottom=std::min(rows-1,unsigned(std::max(0.0F,region.bounds.y+region.bounds.height-1))/kTile);
        bool changedRegion=false; for(unsigned y=top;y<=bottom&&!changedRegion;++y) for(unsigned x=left;x<=right;++x) if(changed[y*columns+x]) {changedRegion=true;break;}
        if(changedRegion) scheduled[region.sourceId-1]++;
      }
      scheduler.observe(scheduled,clockMs()); for(std::size_t i=0;i<scheduled.size();++i) if(scheduled[i]) cache_.invalidateSource(i+1,scheduler.revision(i));
      frameRevision=latest.revision; frame=std::move(latest); changeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-changeStart).count();
    };
    std::unordered_map<std::string,std::string> translations;
    while (!stop.stop_requested()) {
      observe();
      if (frame.texture && columns && rows) {
        const auto jobs=scheduler.takeReady(clockMs(),settings_.stabilityDelayMs,std::size_t(columns)*rows);
        if (!jobs.empty()) try {
          const unsigned blockColumns=(width+kCoreWidth-1)/kCoreWidth; std::map<std::size_t,std::vector<RegionJob>> blockJobs;
          for(const auto job:jobs) { const unsigned cx=std::min(width-1,unsigned(job.tile%columns)*kTile+kTile/2), cy=std::min(height-1,unsigned(job.tile/columns)*kTile+kTile/2); blockJobs[(cy/kCoreHeight)*blockColumns+cx/kCoreWidth].push_back(job); }
          const auto blockLimit=std::clamp(settings_.maxOcrBatch,1U,32U); std::vector<std::pair<std::size_t,std::vector<RegionJob>>> selected;
          for(auto& entry:blockJobs) { if(selected.size()<blockLimit) selected.push_back(std::move(entry)); else for(const auto job:entry.second) scheduler.retry(job); }
          std::unordered_map<std::size_t,SourceWork> sources; std::vector<PendingRegion> pending; const auto snapshot=frame;
          for(const auto& [block,blockWork]:selected) {
            if(!scheduler.matches(blockWork.front().tile,blockWork.front().revision)) continue;
            const auto crop=cropFor(unsigned(block%blockColumns),unsigned(block/blockColumns)); auto pixels=gpu->readCrop(snapshot.texture.Get(),crop.x,crop.y,crop.width,crop.height); ++ocrCrops;
            const auto ocrStart=std::chrono::steady_clock::now(); const auto boxes=detector.detect(pixels,int(crop.width),int(crop.height),int(std::clamp(settings_.detectorLongSide,32U,2048U)),settings_.detectorThreshold); detectorBoxes+=boxes.size(); ocrMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-ocrStart).count();
            std::unordered_map<std::size_t,RegionJob> jobsByTile; for(const auto job:blockWork) { jobsByTile.insert_or_assign(job.tile,job); sources.insert_or_assign(job.tile,SourceWork{job,{}}); }
            std::vector<Rect> accepted;
            for(const auto& box:boxes) {
              const unsigned cx=crop.x+unsigned(box.x+box.width/2), cy=crop.y+unsigned(box.y+box.height/2); if(cx<crop.coreX||cx>=crop.coreX+crop.coreWidth||cy<crop.coreY||cy>=crop.coreY+crop.coreHeight) continue;
              const std::size_t tile=std::min(rows-1,cy/kTile)*columns+std::min(columns-1,cx/kTile); const auto job=jobsByTile.find(tile); if(job==jobsByTile.end()) continue;
              const Rect bounds{float(cx-unsigned(box.width/2)),float(cy-unsigned(box.height/2)),float(box.width),float(box.height)}; bool duplicate=false; for(const auto& previous:accepted) if(overlap(bounds,previous)>0.65F){duplicate=true;break;} if(duplicate) continue; accepted.push_back(bounds);
              std::vector<std::uint8_t> line(std::size_t(box.width)*box.height*4); for(int y=0;y<box.height;++y) std::copy_n(pixels.data()+((std::size_t(box.y+y)*crop.width)+box.x)*4,box.width*4,line.data()+std::size_t(y)*box.width*4);
              const auto recognized=recognizer.recognize(line,box.width,box.height); if(recognized.text.empty()) continue;
              TextRegion region; region.stableId=((tile+1)<<32)|(sources[tile].regions.size()+1); region.german=recognized.text; region.ocrConfidence=recognized.confidence; region.bounds=bounds; region.polygon={{bounds.x,bounds.y},{bounds.x+bounds.width,bounds.y},{bounds.x+bounds.width,bounds.y+bounds.height},{bounds.x,bounds.y+bounds.height}}; region.style.fontPx=std::clamp(bounds.height*0.7F,12.0F,48.0F);
              PendingRegion item{std::move(region),job->second,{}}; const unsigned left=unsigned(std::max(0.0F,bounds.x))/kTile, top=unsigned(std::max(0.0F,bounds.y))/kTile, right=std::min(columns-1,unsigned(std::max(0.0F,bounds.x+bounds.width-1))/kTile), bottom=std::min(rows-1,unsigned(std::max(0.0F,bounds.y+bounds.height-1))/kTile);
              for(unsigned y=top;y<=bottom;++y) for(unsigned x=left;x<=right;++x) { const auto dependency=std::size_t(y)*columns+x; item.dependencies.emplace_back(dependency,scheduler.revision(dependency)); }
              sources[tile].regions.push_back(pending.size()); pending.push_back(std::move(item)); ++recognizedLines;
            }
            observe();
          }
          std::vector<OcrLine> lines; lines.reserve(pending.size());
          for(const auto& item:pending) if(!item.suppressed) lines.push_back({item.region.stableId,item.region.bounds,item.region.ocrConfidence,item.region.german});
          const auto groups=groupTextLines(lines,TextGroupingOptions{settings_.maxHeightRatio,settings_.maxVerticalGapRatio,settings_.maxGroupLines});
          std::unordered_map<std::uint64_t,std::size_t> lineIndex;
          for(std::size_t i=0;i<pending.size();++i) if(!pending[i].suppressed) lineIndex.emplace(pending[i].region.stableId,i);
          for(const auto& group:groups) {
            if(group.members.empty()) continue;
            const auto first=lineIndex.at(group.members.front().stableId); auto& target=pending[first];
            target.region.german=group.source; target.region.bounds=group.bounds; target.region.polygon={{group.bounds.x,group.bounds.y},{group.bounds.x+group.bounds.width,group.bounds.y},{group.bounds.x+group.bounds.width,group.bounds.y+group.bounds.height},{group.bounds.x,group.bounds.y+group.bounds.height}};
            target.dependencies.clear();
            for(const auto& member:group.members) {
              const auto memberIndex=lineIndex.at(member.stableId); if(memberIndex!=first) pending[memberIndex].suppressed=true;
              target.region.ocrConfidence=std::min(target.region.ocrConfidence,member.confidence);
              target.dependencies.insert(target.dependencies.end(),pending[memberIndex].dependencies.begin(),pending[memberIndex].dependencies.end());
            }
            std::sort(target.dependencies.begin(),target.dependencies.end()); target.dependencies.erase(std::unique(target.dependencies.begin(),target.dependencies.end()),target.dependencies.end());
          }
          std::unordered_map<std::string,std::size_t> itemByText; struct TranslationItem{std::string text;std::vector<std::size_t> regions;}; std::vector<TranslationItem> items;
          for(std::size_t i=0;i<pending.size();++i) { auto& item=pending[i]; if(item.suppressed) continue; const auto found=translations.find(item.region.german); if(found!=translations.end()){item.region.english=found->second;item.attempted=true;continue;} const auto [where,inserted]=itemByText.emplace(item.region.german,items.size()); if(inserted) items.push_back({item.region.german,{}}); items[where->second].regions.push_back(i); }
          auto publishReady=[&] { std::vector<RegionCache::SourceReplacement> replacements; std::vector<std::size_t> ready; std::vector<bool> retry;
            for(auto& [tile,work]:sources) { if(work.finished) continue; bool done=true, failed=false; for(const auto index:work.regions) { if(pending[index].suppressed) continue; if(pending[index].region.english.empty()&&!pending[index].attempted) {done=false;break;} if(pending[index].region.english.empty()) failed=true; } if(!done) continue; bool valid=scheduler.current(work.job); for(const auto index:work.regions) if(!pending[index].suppressed) for(const auto [dependency,revision]:pending[index].dependencies) valid=valid&&scheduler.matches(dependency,revision); if(!valid){if(scheduler.current(work.job))scheduler.retry(work.job);work.finished=true;continue;} std::vector<TextRegion> regions; for(const auto index:work.regions) if(!pending[index].suppressed&&!pending[index].region.english.empty()) regions.push_back(std::move(pending[index].region)); replacements.push_back({tile+1,work.job.revision,std::move(regions)}); ready.push_back(tile); retry.push_back(failed); }
            if(!replacements.empty()&&cache_.replaceSources(std::move(replacements))) for(std::size_t i=0;i<ready.size();++i){const auto tile=ready[i];sources[tile].finished=true;if(retry[i])scheduler.retry(sources[tile].job,clockMs()+1000);else scheduler.complete(sources[tile].job);++published;}
          };
          publishReady();
          for(std::size_t first=0;first<items.size()&&!stop.stop_requested();) { std::vector<std::string> german;std::vector<std::size_t> selectedItems;std::size_t characters=0;const auto maxItems=std::clamp(settings_.maxTranslationBatch,1U,16U); while(first<items.size()&&selectedItems.size()<maxItems){const auto size=items[first].text.size();if(!selectedItems.empty()&&characters+size>kMaxTranslationCharacters)break;german.push_back(items[first].text);selectedItems.push_back(first);characters+=size;++first;}
            const auto translationStart=std::chrono::steady_clock::now(); auto request=std::async(std::launch::async,[&translator,german,stop](){return translator->translate(german,stop);}); while(request.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready&&!stop.stop_requested())observe(); const auto english=request.get(); ++modelRequests;translateMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-translationStart).count();
            for(std::size_t i=0;i<selectedItems.size();++i) for(const auto index:items[selectedItems[i]].regions){pending[index].attempted=true;if(i<english.size()&&!english[i].empty()){pending[index].region.english=english[i];translations.insert_or_assign(pending[index].region.german,english[i]);}} if(translations.size()>2048)translations.clear();publishReady();
          }
          publishReady(); for(auto& [_,work]:sources) if(!work.finished&&scheduler.current(work.job)){scheduler.retry(work.job,clockMs()+250);++dropped;}
        } catch(const std::exception& error) { if(stop.stop_requested()) break; ++dropped; if(!translator->alive()){status_(L"Restarting the selected local model...");translator=std::make_unique<LocalTranslator>(settings_,stop);} for(const auto job:jobs) scheduler.retry(job,clockMs()+std::min<std::uint64_t>(10000,1000ULL<<std::min<std::uint64_t>(job.attempt-1,3))); debugLog("Dropped one screen translation batch: "+std::string(error.what())); }
        auto oneDecimal=[](double value){wchar_t text[32]{};swprintf_s(text,L"%.1f",value);return std::wstring(text);}; auto percentile=[&](double fraction){if(frameSamples.empty())return 0.0;std::vector<double> sorted(frameSamples.begin(),frameSamples.end());std::sort(sorted.begin(),sorted.end());return sorted[std::min(sorted.size()-1,std::size_t(fraction*(sorted.size()-1)))];}; std::uint64_t vramMiB{}; if(adapter){DXGI_QUERY_VIDEO_MEMORY_INFO memory{};if(SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory)))vramMiB=memory.CurrentUsage/(1024*1024);}
        status_(L"Local | frame p1/p99 "+oneDecimal(percentile(.01))+L"/"+oneDecimal(percentile(.99))+L"ms | diff "+oneDecimal(changeMs)+L"ms | OCR "+oneDecimal(ocrMs)+L"ms | boxes "+std::to_wstring(detectorBoxes)+L" @"+oneDecimal(settings_.detectorThreshold)+L" | model "+oneDecimal(translateMs)+L"ms | crops "+std::to_wstring(ocrCrops)+L" | model requests "+std::to_wstring(modelRequests)+L" | VRAM "+std::to_wstring(vramMiB)+L"MiB | "+processResources(translator->processId(),processAccounting)+L" | regions "+std::to_wstring(cache_.visible().size())+L" | published "+std::to_wstring(published)+L" | stale "+std::to_wstring(dropped)+L" | lines "+std::to_wstring(recognizedLines));
      }
      const auto delay=std::clamp(settings_.ocrCadenceMs,20U,2000U); for(unsigned elapsed=0;elapsed<delay&&!stop.stop_requested();elapsed+=20)std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  } catch(const std::exception& error) { if(!stop.stop_requested()){status_(std::wstring(L"Translation stopped: ")+winrt::to_hstring(error.what()).c_str());debugLog("Live pipeline failed: "+std::string(error.what()));} }
  if(apartment)winrt::uninit_apartment();
}
}
