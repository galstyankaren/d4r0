#include "d4r0/DiagnosticSession.h"
#include "d4r0/OcrRecognizer.h"
#include "d4r0/TextGrouping.h"
#include "d4r0/TextStability.h"
#include "d4r0/LocalTranslator.h"
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <winrt/base.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

using Microsoft::WRL::ComPtr;
namespace {
std::uint64_t numberAfter(const std::string& line, const std::string& key) {
  auto at=line.find("\""+key+"\":");
  if(at==std::string::npos) return 0;
  at+=key.size()+3;
  try { return std::stoull(line.substr(at)); } catch(...) { return 0; }
}
float floatAfter(const std::string& line, const std::string& key, float fallback) {
  auto at=line.find("\""+key+"\":");
  if(at==std::string::npos) return fallback;
  at+=key.size()+3;
  try { return std::stof(line.substr(at)); } catch(...) { return fallback; }
}
struct ReplayCrop {
  std::uint64_t revision{};
  unsigned x{},y{},width{},height{},longSide{960};
  float threshold{0.3F};
  unsigned coreY{},coreHeight{};
};
std::vector<std::uint8_t> readPng(IWICImagingFactory* factory,
                                  const std::filesystem::path& path, unsigned& width, unsigned& height) {
  ComPtr<IWICBitmapDecoder> decoder;
  if(FAILED(factory->CreateDecoderFromFilename(path.c_str(),nullptr,GENERIC_READ,
      WICDecodeMetadataCacheOnDemand,&decoder))) throw std::runtime_error("PNG decode failed");
  ComPtr<IWICBitmapFrameDecode> frame;
  if(FAILED(decoder->GetFrame(0,&frame))) throw std::runtime_error("PNG frame missing");
  UINT w{},h{}; frame->GetSize(&w,&h); width=w; height=h;
  ComPtr<IWICFormatConverter> converter;
  if(FAILED(factory->CreateFormatConverter(&converter)) ||
     FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,
         WICBitmapDitherTypeNone,nullptr,0,WICBitmapPaletteTypeCustom)))
    throw std::runtime_error("PNG conversion failed");
  std::vector<std::uint8_t> pixels(std::size_t(width)*height*4);
  if(FAILED(converter->CopyPixels(nullptr,width*4,UINT(pixels.size()),pixels.data())))
    throw std::runtime_error("PNG pixels unavailable");
  return pixels;
}
}
int wmain(int argc, wchar_t** argv) {
  if(argc!=5 && argc!=6 && argc!=9) {
    std::wcerr << L"Usage: d4r0_diagnostic_replay <session> <detector.onnx> <recognizer.onnx> <dictionary.yml> [--half-screen|--full-width-strips|--wide-strips|--full-frame-highres [--translate <llama-server.exe> <model.gguf>]]\n";
    return 2;
  }
  try {
    const auto replayMode=argc>=6?std::wstring_view(argv[5]):std::wstring_view{};
    const bool fullWidthStrips=replayMode==L"--full-width-strips";
    const bool wideStrips=replayMode==L"--wide-strips";
    const bool fullFrameHighRes=replayMode==L"--full-frame-highres";
    const bool halfScreen=replayMode==L"--half-screen";
    if(argc>=6 && !fullWidthStrips && !wideStrips && !fullFrameHighRes && !halfScreen) throw std::invalid_argument("Unknown replay option");
    const bool translate=argc==9 && std::wstring_view(argv[6])==L"--translate";
    if(argc==9 && !translate) throw std::invalid_argument("Unknown replay option");
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    CoInitializeEx(nullptr,COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> factory;
    if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
         IID_PPV_ARGS(&factory)))) throw std::runtime_error("Windows Imaging Component unavailable");
    d4r0::OcrDetector detector(argv[2]);
    d4r0::OcrRecognizer recognizer(argv[3],argv[4]);
    std::unique_ptr<d4r0::LocalTranslator> translator;
    if(translate) {
      d4r0::PipelineSettings settings;
      settings.llamaExecutable=argv[7]; settings.model4b=argv[8];
      translator=std::make_unique<d4r0::LocalTranslator>(settings);
    }
    const std::filesystem::path session=argv[1];
    std::map<std::uint64_t,std::uint64_t> revisions;
    std::map<std::uint64_t,std::vector<ReplayCrop>> crops;
    std::ifstream log(session/L"events.jsonl");
    for(std::string line;std::getline(log,line);) {
      if(line.find("\"kind\":\"frame\"")!=std::string::npos)
        revisions[numberAfter(line,"image")]=numberAfter(line,"revision");
      else if(line.find("\"kind\":\"crop\"")!=std::string::npos)
        crops[numberAfter(line,"image")].push_back({numberAfter(line,"revision"),
            unsigned(numberAfter(line,"x")),unsigned(numberAfter(line,"y")),
            unsigned(numberAfter(line,"width")),unsigned(numberAfter(line,"height")),
            unsigned(numberAfter(line,"longSide")),floatAfter(line,"threshold",0.3F)});
    }
    std::ofstream out(session/L"offline-ocr.jsonl",std::ios::trunc);
    if(!out) throw std::runtime_error("Cannot write offline records");
    std::vector<std::pair<std::uint64_t,std::filesystem::path>> sourceImages;
    for(const auto& entry:std::filesystem::directory_iterator(session)) {
      if(!entry.is_regular_file() || entry.path().extension()!=L".png") continue;
      const auto name=entry.path().stem().wstring();
      if(!name.starts_with(L"source-")) continue;
      sourceImages.emplace_back(std::stoull(name.substr(7)),entry.path());
    }
    std::sort(sourceImages.begin(),sourceImages.end());
    d4r0::TextStability stability;
    std::unordered_map<std::string,std::string> translationCache;
    for(const auto& [image,path]:sourceImages) {
      stability.prune(image*2000);
      unsigned width{},height{};
      auto pixels=readPng(factory.Get(),path,width,height);
      std::vector<d4r0::OcrLine> lines;
      std::uint64_t nextLineId{};
      out << "{\"kind\":\"frame\",\"image\":" << image << ",\"revision\":"
          << revisions[image] << "}\n";
      auto recorded=crops[image];
      std::vector<ReplayCrop> exact;
      for(const auto& crop:recorded) if(crop.revision==revisions[image] &&
          crop.width && crop.height && crop.x+crop.width<=width && crop.y+crop.height<=height)
        exact.push_back(crop);
      const bool exactMatch=!exact.empty();
      if(halfScreen) {
        exact.clear();
        const unsigned coreHeight=(height+1)/2;
        for(unsigned coreY=0;coreY<height;coreY+=coreHeight) {
          const unsigned y=coreY>64?coreY-64:0;
          const unsigned bottom=std::min(height,coreY+coreHeight+64);
          exact.push_back({revisions[image],0,y,width,bottom-y,2048,0.3F,
                           coreY,std::min(coreHeight,height-coreY)});
        }
      } else if(fullWidthStrips || wideStrips) {
        exact.clear();
        const unsigned coreWidth=wideStrips?1920:width;
        for(unsigned top=0;top<height;top+=540) {
          const unsigned y=top>64?top-64:0, bottom=std::min(height,top+540+64);
          for(unsigned left=0;left<width;left+=coreWidth) {
            const unsigned x=wideStrips && left>512?left-512:0;
            const unsigned right=std::min(width,left+coreWidth+(wideStrips?512:0));
            exact.push_back({revisions[image],x,y,right-x,bottom-y,2048,0.3F});
          }
        }
      } else if(fullFrameHighRes) exact.assign(1,{revisions[image],0,0,width,height,2048,0.3F});
      else if(exact.empty()) exact.push_back({revisions[image],0,0,width,height,960,0.3F});
      for(const auto& area:exact) {
        std::vector<std::uint8_t> input(std::size_t(area.width)*area.height*4);
        for(unsigned y=0;y<area.height;++y)
          std::copy_n(pixels.data()+(std::size_t(area.y+y)*width+area.x)*4,
                      std::size_t(area.width)*4,input.data()+std::size_t(y)*area.width*4);
        const auto boxes=detector.detect(input,int(area.width),int(area.height),
                                         int(area.longSide ? area.longSide : 960),area.threshold);
        out << "{\"kind\":\"crop\",\"image\":" << image << ",\"revision\":"
            << revisions[image] << ",\"mode\":\"" << (halfScreen?"halfScreen":fullWidthStrips?"fullWidthStrip":wideStrips?"wideStrip":fullFrameHighRes?"fullFrameHighRes":exactMatch?"recordedCrop":"fullFrame")
            << "\",\"x\":" << area.x << ",\"y\":" << area.y
            << ",\"width\":" << area.width << ",\"height\":" << area.height
            << ",\"boxes\":" << boxes.size() << "}\n";
        for(const auto& box:boxes) {
          const unsigned centerY=area.y+unsigned(box.y+box.height/2);
          if(halfScreen && (centerY<area.coreY || centerY>=area.coreY+area.coreHeight)) continue;
          std::vector<std::uint8_t> line(std::size_t(box.width)*box.height*4);
          for(int y=0;y<box.height;++y)
            std::copy_n(input.data()+(std::size_t(box.y+y)*area.width+box.x)*4,box.width*4,
                        line.data()+std::size_t(y)*box.width*4);
          const auto result=recognizer.recognize(line,box.width,box.height);
          const d4r0::Rect bounds{float(area.x+box.x),float(area.y+box.y),
                                   float(box.width),float(box.height)};
          const bool confirmed=stability.observe(bounds,result.text,result.confidence,image*2000);
          if(!result.text.empty()) {
            d4r0::OcrLine recognized{++nextLineId,bounds,result.confidence,result.text};
            lines.push_back(std::move(recognized));
          }
          out << "{\"kind\":\"ocr\",\"image\":" << image << ",\"revision\":"
              << revisions[image] << ",\"x\":" << area.x+box.x << ",\"y\":" << area.y+box.y
              << ",\"width\":" << box.width << ",\"height\":" << box.height
              << ",\"detectorConfidence\":" << box.confidence << ",\"text\":"
              << d4r0::DiagnosticSession::quote(result.text) << ",\"confidence\":"
              << result.confidence << ",\"confirmed\":" << (confirmed?"true":"false") << "}\n";
        }
      }
      auto groups=d4r0::groupTextLines(lines);
      for(const auto& group:groups)
        out << "{\"kind\":\"group\",\"image\":" << image << ",\"x\":"
            << group.bounds.x << ",\"y\":" << group.bounds.y << ",\"width\":"
            << group.bounds.width << ",\"height\":" << group.bounds.height
            << ",\"lines\":" << group.members.size() << ",\"text\":"
            << d4r0::DiagnosticSession::quote(group.source) << "}\n";
      if(translator) {
        std::vector<std::string> missing;
        auto translateMissing=[&] {
          if(missing.empty()) return;
          const auto english=translator->translate(missing);
          for(std::size_t i=0;i<missing.size();++i)
            translationCache.emplace(missing[i],i<english.size()?english[i]:"");
          missing.clear();
        };
        for(const auto& group:groups) {
          if(translationCache.contains(group.source) ||
              std::find(missing.begin(),missing.end(),group.source)!=missing.end()) continue;
          missing.push_back(group.source);
          if(missing.size()==8) translateMissing();
        }
        translateMissing();
        for(const auto& group:groups)
          out << "{\"kind\":\"translation\",\"image\":" << image
              << ",\"source\":" << d4r0::DiagnosticSession::quote(group.source)
              << ",\"result\":" << d4r0::DiagnosticSession::quote(translationCache.at(group.source))
              << "}\n";
        out.flush();
      }
    }
    translator.reset();
    factory.Reset();
    CoUninitialize();
    winrt::uninit_apartment();
  } catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
