#include "d4r0/LivePipeline.h"
#include "d4r0/GpuRegions.h"
#include "d4r0/OcrRecognizer.h"
#include "d4r0/LocalTranslator.h"
#include "d4r0/RegionScheduler.h"
#include "d4r0/DebugLog.h"
#include "d4r0/TextGrouping.h"
#include "d4r0/TextStability.h"
#include <winrt/base.h>
#include <algorithm>
#include <chrono>
#include <cwchar>
#include <cstring>
#include <deque>
#include <dxgi1_4.h>
#include <psapi.h>
#include <future>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace d4r0 {
namespace {
constexpr unsigned kTile = GpuRegions::tileSize;
constexpr unsigned kMarginY = 64;
constexpr std::size_t kMaxTranslationCharacters = 6000;
struct Crop { unsigned x{}, y{}, width{}, height{}, coreY{}, coreHeight{}; };
struct PendingRegion {
  TextRegion region;
  bool attempted{}; bool suppressed{};
};
struct SourceWork { std::vector<RegionJob> jobs; std::vector<std::size_t> regions; bool finished{}, unconfirmed{}; };
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
std::vector<std::uint8_t> readDiagnosticFrame(ID3D11Texture2D* source) {
  D3D11_TEXTURE2D_DESC desc{}; source->GetDesc(&desc);
  Microsoft::WRL::ComPtr<ID3D11Device> device;
  Microsoft::WRL::ComPtr<ID3D11DeviceContext> context;
  source->GetDevice(&device);
  device->GetImmediateContext(&context);
  desc.Usage=D3D11_USAGE_STAGING; desc.BindFlags=0;
  desc.CPUAccessFlags=D3D11_CPU_ACCESS_READ; desc.MiscFlags=0;
  Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
  winrt::check_hresult(device->CreateTexture2D(&desc,nullptr,&staging));
  context->CopyResource(staging.Get(),source);
  D3D11_MAPPED_SUBRESOURCE mapped{};
  winrt::check_hresult(context->Map(staging.Get(),0,D3D11_MAP_READ,0,&mapped));
  std::vector<std::uint8_t> pixels(std::size_t(desc.Width)*desc.Height*4);
  for(unsigned y=0;y<desc.Height;++y)
    std::memcpy(pixels.data()+std::size_t(y)*desc.Width*4,
                static_cast<const std::uint8_t*>(mapped.pData)+std::size_t(y)*mapped.RowPitch,
                std::size_t(desc.Width)*4);
  context->Unmap(staging.Get(),0);
  return pixels;
}
}

LivePipeline::LivePipeline(PipelineSettings settings, WindowsGraphicsCapture& capture, RegionCache& cache,
                           DiagnosticSession& diagnostics, std::function<void(std::wstring)> status)
    : settings_(std::move(settings)), capture_(capture), cache_(cache), diagnostics_(diagnostics),
      status_(std::move(status)),
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
    RegionScheduler scheduler; TextStability stability; unsigned width{}, height{}, columns{}, rows{};
    std::uint64_t frameRevision{}, published{}, dropped{}, ocrCrops{}, modelRequests{}, recognizedLines{}, detectorBoxes{}; ProcessAccounting processAccounting;
    double frameMs{}, changeMs{}, ocrMs{}, translateMs{}; std::deque<double> frameSamples;
    auto lastFrame = std::chrono::steady_clock::time_point{}; CapturedFrame frame;
    auto cropFor = [&](unsigned blockY) {
      const unsigned coreHeight=(height+1)/2, coreY=blockY*coreHeight;
      const unsigned y=coreY>kMarginY?coreY-kMarginY:0;
      const unsigned bottom=std::min(height,coreY+coreHeight+kMarginY);
      return Crop{0,y,width,bottom-y,coreY,std::min(coreHeight,height-coreY)};
    };
    auto observe = [&] {
      auto latest = capture_.latestFrame(); if (latest.revision == frameRevision) return;
      if (!latest.texture) { frameRevision=latest.revision; frame={}; gpu.reset(); adapter.Reset(); width=height=columns=rows=0; scheduler.reset(0); stability.clear(); cache_.clear(); return; }
      const auto changeStart = std::chrono::steady_clock::now();
      if (lastFrame != std::chrono::steady_clock::time_point{}) frameMs=std::chrono::duration<double,std::milli>(changeStart-lastFrame).count();
      lastFrame=changeStart; if (frameMs>0) { frameSamples.push_back(frameMs); if(frameSamples.size()>256) frameSamples.pop_front(); }
      D3D11_TEXTURE2D_DESC description{}; latest.texture->GetDesc(&description);
      if (!gpu) {
        Microsoft::WRL::ComPtr<ID3D11Device> device; latest.texture->GetDevice(&device); gpu=std::make_unique<GpuRegions>(device.Get());
        Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
        if (SUCCEEDED(device.As(&dxgiDevice))) { Microsoft::WRL::ComPtr<IDXGIAdapter> baseAdapter; if(SUCCEEDED(dxgiDevice->GetAdapter(&baseAdapter))) baseAdapter.As(&adapter); }
      }
      if (width != description.Width || height != description.Height) { width=description.Width; height=description.Height; columns=(width+kTile-1)/kTile; rows=(height+kTile-1)/kTile; scheduler.reset(columns*rows); stability.clear(); cache_.clear(); }
      const auto changed=gpu->compare(latest.texture.Get());
      // Pixel motion alone is not a text revision. Keep the last verified text
      // visible until OCR replaces or clears it; in-flight jobs still use tile revisions.
      scheduler.observe(changed,clockMs());
      frameRevision=latest.revision; frame=std::move(latest); changeMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-changeStart).count();
      const auto now=clockMs();
      if (diagnostics_.sourceDue(now)) {
        try {
          auto image=readDiagnosticFrame(frame.texture.Get());
          diagnostics_.recordSource(frameRevision,now,width,height,image);
          const auto active=std::count_if(changed.begin(),changed.end(),[](auto count){return count!=0;});
          diagnostics_.event("tiles",frameRevision,"\"active\":"+std::to_string(active)+
              ",\"total\":"+std::to_string(changed.size())+
              ",\"changeMs\":"+std::to_string(changeMs));
        } catch(const std::exception& error) {
          diagnostics_.event("error",frameRevision,"\"stage\":\"sourceCapture\",\"message\":"+
              DiagnosticSession::quote(error.what()));
        }
      }
    };
    std::unordered_map<std::string,std::string> translations;
    while (!stop.stop_requested()) {
      observe();
      if (frame.texture && columns && rows) {
        const auto jobs=scheduler.takeReady(clockMs(),settings_.stabilityDelayMs,std::size_t(columns)*rows);
        if(!jobs.empty() && diagnostics_.enabled()) diagnostics_.event("schedule",frameRevision,
            "\"jobs\":"+std::to_string(jobs.size()));
        if (!jobs.empty()) try {
          std::map<std::size_t,std::vector<RegionJob>> blockJobs;
          const unsigned coreHeight=(height+1)/2;
          for(const auto job:jobs) {
            const unsigned cy=std::min(height-1,unsigned(job.tile/columns)*kTile+kTile/2);
            blockJobs[cy/coreHeight].push_back(job);
          }
          std::vector<std::pair<std::size_t,std::vector<RegionJob>>> selected;
          for(auto& entry:blockJobs) selected.push_back(std::move(entry));
          std::unordered_map<std::size_t,SourceWork> sources; std::vector<PendingRegion> pending; const auto snapshot=frame;
          stability.prune(clockMs());
          const auto diagnosticNow=clockMs();
          if (diagnostics_.sourceDue(diagnosticNow,500)) {
            try {
              auto image=readDiagnosticFrame(snapshot.texture.Get());
              diagnostics_.recordSource(snapshot.revision,diagnosticNow,width,height,image,500);
            } catch(const std::exception& error) {
              diagnostics_.event("error",snapshot.revision,"\"stage\":\"ocrFrameCapture\",\"message\":"+
                  DiagnosticSession::quote(error.what()));
            }
          }
          for(const auto& [block,blockWork]:selected) {
            const auto crop=cropFor(unsigned(block)); auto pixels=gpu->readCrop(snapshot.texture.Get(),crop.x,crop.y,crop.width,crop.height); ++ocrCrops;
            const auto longSide=width>1920 ? 2048U : std::clamp(settings_.detectorLongSide,32U,2048U);
            const auto ocrStart=std::chrono::steady_clock::now(); const auto boxes=detector.detect(pixels,int(crop.width),int(crop.height),int(longSide),settings_.detectorThreshold); detectorBoxes+=boxes.size(); ocrMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-ocrStart).count();
            if(diagnostics_.enabled()) diagnostics_.event("crop",snapshot.revision,
                "\"x\":"+std::to_string(crop.x)+",\"y\":"+std::to_string(crop.y)+
                ",\"width\":"+std::to_string(crop.width)+",\"height\":"+std::to_string(crop.height)+
                ",\"boxes\":"+std::to_string(boxes.size())+",\"detectorMs\":"+std::to_string(ocrMs)+
                ",\"longSide\":"+std::to_string(longSide)+
                ",\"threshold\":"+std::to_string(settings_.detectorThreshold));
            sources.insert_or_assign(block,SourceWork{blockWork,{}});
            const std::uint64_t sourceId=std::uint64_t(columns)*rows+block+1;
            std::vector<Rect> accepted;
            for(const auto& box:boxes) {
              const unsigned cx=crop.x+unsigned(box.x+box.width/2), cy=crop.y+unsigned(box.y+box.height/2);
              if(cy<crop.coreY || cy>=crop.coreY+crop.coreHeight) continue;
              const std::size_t tile=std::min(rows-1,cy/kTile)*columns+std::min(columns-1,cx/kTile);
              const Rect bounds{float(cx-unsigned(box.width/2)),float(cy-unsigned(box.height/2)),float(box.width),float(box.height)};
              const auto boxFields="\"x\":"+std::to_string(bounds.x)+",\"y\":"+std::to_string(bounds.y)+
                  ",\"width\":"+std::to_string(bounds.width)+",\"height\":"+std::to_string(bounds.height)+
                  ",\"tile\":"+std::to_string(tile)+",\"tileRevision\":"+std::to_string(blockWork.front().revision)+
                  ",\"detectorConfidence\":"+std::to_string(box.confidence);
              bool duplicate=false; for(const auto& previous:accepted) if(overlap(bounds,previous)>0.65F){duplicate=true;break;}
              if(duplicate) { diagnostics_.event("rejected",snapshot.revision,boxFields+",\"reason\":\"duplicate\""); continue; }
              accepted.push_back(bounds);
              std::vector<std::uint8_t> line(std::size_t(box.width)*box.height*4); for(int y=0;y<box.height;++y) std::copy_n(pixels.data()+((std::size_t(box.y+y)*crop.width)+box.x)*4,box.width*4,line.data()+std::size_t(y)*box.width*4);
              auto recognized=recognizer.recognize(line,box.width,box.height);
              if (box.height < 28 && recognized.confidence < settings_.minimumOcrConfidence) {
                auto enhanced=line;
                int low=255, high=0;
                for(std::size_t p=0;p<line.size();p+=4) { const int light=(int(line[p])+int(line[p+1])+int(line[p+2]))/3; low=std::min(low,light); high=std::max(high,light); }
                if(high-low>16) {
                  for(std::size_t p=0;p<line.size();p+=4) for(int channel=0;channel<3;++channel)
                    enhanced[p+channel]=std::uint8_t(std::clamp((int(line[p+channel])-low)*255/(high-low),0,255));
                  auto retry=recognizer.recognize(enhanced,box.width,box.height);
                  if(retry.confidence>recognized.confidence) recognized=std::move(retry);
                }
              }
              if(recognized.text.empty()) {
                diagnostics_.event("rejected",snapshot.revision,boxFields+",\"reason\":\"emptyRecognition\"");
                continue;
              }
              diagnostics_.event("ocr",snapshot.revision,boxFields+",\"text\":"+
                  DiagnosticSession::quote(recognized.text)+",\"confidence\":"+
                  std::to_string(recognized.confidence));
              if(!stability.observe(bounds,recognized.text,recognized.confidence,clockMs())) {
                sources[block].unconfirmed=true;
                diagnostics_.event("rejected",snapshot.revision,boxFields+",\"reason\":\"unstableOrFragment\"");
                continue;
              }
              TextRegion region; region.stableId=(sourceId<<32)|(sources[block].regions.size()+1); region.german=recognized.text; region.ocrConfidence=recognized.confidence; region.bounds=bounds; region.polygon={{bounds.x,bounds.y},{bounds.x+bounds.width,bounds.y},{bounds.x+bounds.width,bounds.y+bounds.height},{bounds.x,bounds.y+bounds.height}}; region.style.fontPx=std::clamp(bounds.height*0.7F,12.0F,48.0F);
              sources[block].regions.push_back(pending.size()); pending.push_back({std::move(region)}); ++recognizedLines;
            }
            observe();
          }
          std::vector<TextGroup> groups;
          std::vector<OcrLine> lines;
          for(const auto& [block,work]:sources)
            for(const auto index:work.regions) {
              const auto& region=pending[index].region;
              lines.push_back({region.stableId,region.bounds,region.ocrConfidence,region.german});
            }
          groups=groupTextLines(lines,TextGroupingOptions{settings_.maxHeightRatio,settings_.maxVerticalGapRatio,settings_.maxGroupLines});
          std::unordered_set<std::uint64_t> groupedIds;
          for(const auto& group:groups) for(const auto& member:group.members) groupedIds.insert(member.stableId);
          for(auto& item:pending) if(!groupedIds.contains(item.region.stableId)) item.suppressed=true;
          if(diagnostics_.enabled()) for(const auto& group:groups) {
            std::string members="[";
            for(const auto& member:group.members) { if(members.size()>1) members+=','; members+=std::to_string(member.stableId); }
            members+=']';
            diagnostics_.event("group",snapshot.revision,"\"members\":"+members+
                ",\"text\":"+DiagnosticSession::quote(group.source));
          }
          std::unordered_map<std::uint64_t,std::size_t> lineIndex;
          for(std::size_t i=0;i<pending.size();++i) if(!pending[i].suppressed) lineIndex.emplace(pending[i].region.stableId,i);
          for(const auto& group:groups) {
            if(group.members.empty()) continue;
            const auto first=lineIndex.at(group.members.front().stableId); auto& target=pending[first];
            target.region.german=group.source; target.region.bounds=group.bounds; target.region.polygon={{group.bounds.x,group.bounds.y},{group.bounds.x+group.bounds.width,group.bounds.y},{group.bounds.x+group.bounds.width,group.bounds.y+group.bounds.height},{group.bounds.x,group.bounds.y+group.bounds.height}};
            for(const auto& member:group.members) {
              const auto memberIndex=lineIndex.at(member.stableId); if(memberIndex!=first) pending[memberIndex].suppressed=true;
              target.region.ocrConfidence=std::min(target.region.ocrConfidence,member.confidence);
            }
          }
          std::unordered_map<std::string,std::size_t> itemByText; struct TranslationItem{std::string text;std::vector<std::size_t> regions;}; std::vector<TranslationItem> items;
          for(std::size_t i=0;i<pending.size();++i) { auto& item=pending[i]; if(item.suppressed) continue; const auto found=translations.find(item.region.german); if(found!=translations.end()){item.region.english=found->second;item.attempted=true;continue;} const auto [where,inserted]=itemByText.emplace(item.region.german,items.size()); if(inserted) items.push_back({item.region.german,{}}); items[where->second].regions.push_back(i); }
          auto publishReady=[&] {
            for(auto& [block,work]:sources) {
              if(work.finished) continue;
              bool done=true, failed=work.unconfirmed;
              for(const auto index:work.regions) {
                if(pending[index].suppressed) continue;
                if(pending[index].region.english.empty()&&!pending[index].attempted) {done=false;break;}
                if(pending[index].region.english.empty()) failed=true;
              }
              if(!done) continue;
              std::vector<TextRegion> regions;
              for(const auto index:work.regions)
                if(!pending[index].suppressed&&!pending[index].region.english.empty())
                  regions.push_back(std::move(pending[index].region));
              if(regions.empty()&&failed) {
                for(const auto job:work.jobs) scheduler.retry(job,clockMs()+250);
                work.finished=true;
                continue;
              }
              const auto sourceId=std::uint64_t(columns)*rows+block+1;
              if(cache_.replaceSource(sourceId,snapshot.revision,std::move(regions))) {
                for(const auto job:work.jobs) {
                  if(failed) scheduler.retry(job,clockMs()+1000);
                  else scheduler.complete(job);
                }
                ++published;
              } else {
                for(const auto job:work.jobs) scheduler.retry(job,clockMs()+250);
              }
              work.finished=true;
            }
          };
          publishReady();
          for(std::size_t first=0;first<items.size()&&!stop.stop_requested();) { std::vector<std::string> german;std::vector<std::size_t> selectedItems;std::size_t characters=0;const auto maxItems=std::clamp(settings_.maxTranslationBatch,1U,16U); while(first<items.size()&&selectedItems.size()<maxItems){const auto size=items[first].text.size();if(!selectedItems.empty()&&characters+size>kMaxTranslationCharacters)break;german.push_back(items[first].text);selectedItems.push_back(first);characters+=size;++first;}
            if(diagnostics_.enabled()) for(const auto& text:german)
              diagnostics_.event("translationRequest",snapshot.revision,"\"text\":"+DiagnosticSession::quote(text));
            const auto translationStart=std::chrono::steady_clock::now(); auto request=std::async(std::launch::async,[&translator,german,stop](){return translator->translate(german,stop);}); while(request.wait_for(std::chrono::milliseconds(20))!=std::future_status::ready&&!stop.stop_requested())observe(); const auto english=request.get(); ++modelRequests;translateMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-translationStart).count();
            if(diagnostics_.enabled()) for(std::size_t i=0;i<selectedItems.size();++i)
              diagnostics_.event("translationResult",snapshot.revision,
                  "\"source\":"+DiagnosticSession::quote(items[selectedItems[i]].text)+
                  ",\"result\":"+DiagnosticSession::quote(i<english.size()?english[i]:"")+
                  ",\"translationMs\":"+std::to_string(translateMs));
            for(std::size_t i=0;i<selectedItems.size();++i) for(const auto index:items[selectedItems[i]].regions){pending[index].attempted=true;if(i<english.size()&&!english[i].empty()){pending[index].region.english=english[i];translations.insert_or_assign(pending[index].region.german,english[i]);}} if(translations.size()>2048)translations.clear();publishReady();
          }
          publishReady(); for(auto& [_,work]:sources) if(!work.finished) {
            for(const auto job:work.jobs) scheduler.retry(job,clockMs()+250);
            ++dropped;
          }
        } catch(const std::exception& error) { if(stop.stop_requested()) break; ++dropped;
          diagnostics_.event("error",frameRevision,"\"stage\":\"pipeline\",\"message\":"+
              DiagnosticSession::quote(error.what()));
          if(!translator->alive()){status_(L"Restarting the selected local model...");translator=std::make_unique<LocalTranslator>(settings_,stop);} for(const auto job:jobs) scheduler.retry(job,clockMs()+std::min<std::uint64_t>(10000,1000ULL<<std::min<std::uint64_t>(job.attempt-1,3))); debugLog("Dropped one screen translation batch: "+std::string(error.what())); }
        auto oneDecimal=[](double value){wchar_t text[32]{};swprintf_s(text,L"%.1f",value);return std::wstring(text);}; auto percentile=[&](double fraction){if(frameSamples.empty())return 0.0;std::vector<double> sorted(frameSamples.begin(),frameSamples.end());std::sort(sorted.begin(),sorted.end());return sorted[std::min(sorted.size()-1,std::size_t(fraction*(sorted.size()-1)))];}; std::uint64_t vramMiB{}; if(adapter){DXGI_QUERY_VIDEO_MEMORY_INFO memory{};if(SUCCEEDED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&memory)))vramMiB=memory.CurrentUsage/(1024*1024);}
        status_(L"Local | frame p1/p99 "+oneDecimal(percentile(.01))+L"/"+oneDecimal(percentile(.99))+L"ms | diff "+oneDecimal(changeMs)+L"ms | OCR "+oneDecimal(ocrMs)+L"ms | boxes "+std::to_wstring(detectorBoxes)+L" @"+oneDecimal(settings_.detectorThreshold)+L" | model "+oneDecimal(translateMs)+L"ms | crops "+std::to_wstring(ocrCrops)+L" | model requests "+std::to_wstring(modelRequests)+L" | VRAM "+std::to_wstring(vramMiB)+L"MiB | "+processResources(translator->processId(),processAccounting)+L" | regions "+std::to_wstring(cache_.visible().size())+L" | published "+std::to_wstring(published)+L" | stale "+std::to_wstring(dropped)+L" | lines "+std::to_wstring(recognizedLines));
      }
      const auto delay=std::clamp(settings_.ocrCadenceMs,20U,2000U); for(unsigned elapsed=0;elapsed<delay&&!stop.stop_requested();elapsed+=20)std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  } catch(const std::exception& error) { if(!stop.stop_requested()){status_(std::wstring(L"Translation stopped: ")+winrt::to_hstring(error.what()).c_str());debugLog("Live pipeline failed: "+std::string(error.what()));} }
  if(apartment)winrt::uninit_apartment();
}
}
