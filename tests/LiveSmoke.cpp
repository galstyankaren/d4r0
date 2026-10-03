#include "d4r0/LivePipeline.h"
#include "d4r0/OverlayWindow.h"
#include "d4r0/DebugLog.h"
#include <winrt/base.h>
#include <iostream>
#include <chrono>
#include <algorithm>
#include <vector>
#include <string_view>
#include <wincodec.h>
#include <wrl/client.h>

namespace {
std::vector<std::uint8_t> sourcePixels;
unsigned sourceWidth{}, sourceHeight{};
void loadSourceImage(const wchar_t* path) {
  Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
  Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
  Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
  Microsoft::WRL::ComPtr<IWICFormatConverter> converter;
  if (FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&factory))) ||
      FAILED(factory->CreateDecoderFromFilename(path,nullptr,GENERIC_READ,WICDecodeMetadataCacheOnDemand,&decoder)) ||
      FAILED(decoder->GetFrame(0,&frame)) || FAILED(frame->GetSize(&sourceWidth,&sourceHeight)) ||
      FAILED(factory->CreateFormatConverter(&converter)) ||
      FAILED(converter->Initialize(frame.Get(),GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,nullptr,0,
                                   WICBitmapPaletteTypeCustom))) throw std::runtime_error("Cannot load source image");
  sourcePixels.resize(std::size_t(sourceWidth)*sourceHeight*4);
  if (FAILED(converter->CopyPixels(nullptr,sourceWidth*4,UINT(sourcePixels.size()),sourcePixels.data())))
    throw std::runtime_error("Cannot decode source image");
}
LRESULT CALLBACK sourceProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
  if (message == WM_PAINT) {
    PAINTSTRUCT paint{}; auto dc = BeginPaint(window,&paint);
    if (!sourcePixels.empty()) {
      RECT client{}; GetClientRect(window,&client);
      BITMAPINFO bitmap{}; bitmap.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
      bitmap.bmiHeader.biWidth=LONG(sourceWidth); bitmap.bmiHeader.biHeight=-LONG(sourceHeight);
      bitmap.bmiHeader.biPlanes=1; bitmap.bmiHeader.biBitCount=32; bitmap.bmiHeader.biCompression=BI_RGB;
      StretchDIBits(dc,0,0,client.right,client.bottom,0,0,sourceWidth,sourceHeight,
                    sourcePixels.data(),&bitmap,DIB_RGB_COLORS,SRCCOPY);
      EndPaint(window,&paint); return 0;
    }
    FillRect(dc,&paint.rcPaint,static_cast<HBRUSH>(GetStockObject(WHITE_BRUSH)));
    auto font = CreateFontW(-38,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto old = SelectObject(dc,font); SetTextColor(dc,RGB(0,0,0)); SetBkMode(dc,TRANSPARENT);
    const std::vector<std::wstring> lines{
      L"Die Welt ist voller Wunder.", L"Speichern", L"Abbrechen", L"Neues Spiel",
      L"Spiel laden", L"Optionen", L"Beenden", L"Leben: 42", L"Mana: 10",
      L"Druecke Ctrl+X"};
    for (std::size_t i = 0; i < lines.size(); ++i)
      TextOutW(dc,80,100+int(i)*110,lines[i].data(),int(lines[i].size()));
    SelectObject(dc,old); DeleteObject(font); EndPaint(window,&paint); return 0;
  }
  return DefWindowProcW(window,message,wparam,lparam);
}
struct Window { HWND value{}; ~Window() { if (value) DestroyWindow(value); } };
}
int wmain(int argc, wchar_t** argv) {
  const bool imageMode=argc>=5 && std::wstring_view(argv[2])==L"--image" &&
      std::wstring_view(argv[4])==L"--diagnostics";
  if (!imageMode && (argc < 2 || argc > 3 ||
      (argc == 3 && std::wstring_view(argv[2]) != L"--diagnostics"))) return 2;
  d4r0::initializeDebugLog(std::filesystem::path(argv[0]).parent_path()/L"live-smoke.log");
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  winrt::init_apartment(winrt::apartment_type::multi_threaded);
  int result = 1;
  try {
    if (imageMode) loadSourceImage(argv[3]);
    const auto assets = std::filesystem::absolute(argv[1]);
    d4r0::PipelineSettings settings;
    settings.llamaExecutable = assets/L"llama-b11068-vulkan/llama-server.exe";
    settings.model4b = assets/L"translategemma-4b-it.Q4_K_M.gguf";
    settings.ocrDetector = assets/L"ocr-det/inference.onnx";
    settings.ocrRecognizer = assets/L"ocr-rec/inference.onnx";
    settings.ocrDictionary = assets/L"ocr-rec/inference.yml";
    const auto instance = GetModuleHandleW(nullptr);
    WNDCLASSW type{}; type.hInstance = instance; type.lpszClassName = L"d4r0LiveSource"; type.lpfnWndProc = sourceProc;
    if (!RegisterClassW(&type)) throw std::runtime_error("Cannot register live test source");
    RECT monitor{};
    EnumDisplayMonitors(nullptr,nullptr,[](HMONITOR,HDC,LPRECT rect,LPARAM data)->BOOL {
      *reinterpret_cast<RECT*>(data) = *rect; return FALSE;
    },reinterpret_cast<LPARAM>(&monitor));
    Window source{CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,type.lpszClassName,L"Synthetic d4r0 test",
        WS_POPUP,monitor.left,monitor.top,monitor.right-monitor.left,monitor.bottom-monitor.top,nullptr,nullptr,instance,nullptr)};
    if (!source.value) throw std::runtime_error("Cannot create live test source");
    ShowWindow(source.value,SW_SHOWNOACTIVATE); UpdateWindow(source.value);
    d4r0::RegionCache cache;
    d4r0::DiagnosticSession diagnostics(std::filesystem::temp_directory_path() / "d4r0-live-smoke-diagnostics");
    d4r0::OverlayWindow overlay(settings,cache,diagnostics);
    if (!overlay.create(instance)) throw std::runtime_error("Cannot create test overlay; check hotkey conflicts");
    if (argc == 3 || imageMode) overlay.setDiagnostics(true);
    d4r0::WindowsGraphicsCapture capture(0);
    if (!capture.start()) throw std::runtime_error("Cannot capture test source");
    overlay.setSourceCapture(&capture);
    std::mutex statusMutex;
    std::wstring status;
    auto readyAt = std::chrono::steady_clock::time_point{};
    auto resultAt = std::chrono::steady_clock::time_point{};
    {
      d4r0::LivePipeline pipeline(settings,capture,cache,diagnostics,[&](std::wstring value) {
        overlay.setStatus(value); std::scoped_lock lock(statusMutex);
        if (readyAt == std::chrono::steady_clock::time_point{} && value.find(L"Translation ready") != std::wstring::npos)
          readyAt = std::chrono::steady_clock::now();
        status = std::move(value);
      });
      const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(imageMode?180:45);
      while (std::chrono::steady_clock::now() < deadline) {
        MSG message{};
        while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
        const auto visible = cache.visible();
        bool found = imageMode;
        for (const auto& region : visible)
          if (region.sourceText == "Die Welt ist voller Wunder." && region.translatedText == "The world is full of wonders.") found = true;
        if (imageMode) for (int argument=5;argument<argc;++argument) {
          const auto expected=winrt::to_string(winrt::hstring(argv[argument]));
          if (std::none_of(visible.begin(),visible.end(),[&](const auto& region) {
                return region.sourceText.find(expected) != std::string::npos && !region.translatedText.empty();
              })) found=false;
        }
        std::chrono::steady_clock::time_point readySnapshot;
        { std::scoped_lock lock(statusMutex); readySnapshot = readyAt; }
        const auto readyElapsed = readySnapshot == std::chrono::steady_clock::time_point{}
            ? std::chrono::milliseconds::max()
            : std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-readySnapshot);
        if (found && visible.size() >= (imageMode?12U:8U) &&
            (imageMode || readyElapsed <= std::chrono::seconds(10))) {
          resultAt = std::chrono::steady_clock::now();
          if (imageMode) diagnostics.recordSource(capture.latestFrame().revision+10000,
              std::uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                  std::chrono::steady_clock::now().time_since_epoch()).count()),
              sourceWidth,sourceHeight,sourcePixels,0);
          overlay.render();
          PostMessageW(overlay.hwnd(),WM_HOTKEY,2,0); // Immediate original mode.
          while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
          if (overlay.mode() != d4r0::DisplayMode::Original || cache.visible().empty())
            throw std::runtime_error("Original mode lost the cache");
          PostMessageW(overlay.hwnd(),WM_HOTKEY,1,0);
          while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
          if (overlay.mode() != d4r0::DisplayMode::Translation) throw std::runtime_error("Translation mode did not restore");
          result = 0; break;
        }
        { std::scoped_lock lock(statusMutex);
          if (status.starts_with(L"Translation stopped:")) { std::wcerr << status << '\n'; break; }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    }
    if (argc == 3 || imageMode) {
      const auto directory=diagnostics.directory();
      diagnostics.setEnabled(false);
      std::wcout << L"DiagnosticSession=" << directory.wstring() << L"\n";
      if (!std::filesystem::exists(directory/L"source-1.png") ||
          !std::filesystem::exists(directory/L"overlay-1.png") ||
          !std::filesystem::exists(directory/L"events.jsonl"))
        throw std::runtime_error("Diagnostic image pair or log missing");
    }
    if (result) {
      std::scoped_lock lock(statusMutex);
      std::wcerr << L"Final status: " << status << L"\n";
      std::wcerr << L"Visible regions: " << cache.visible().size() << L"\n";
      for (const auto& region:cache.visible())
        std::cerr << "  " << region.sourceText << " -> " << region.translatedText << "\n";
      if (readyAt != std::chrono::steady_clock::time_point{})
        std::wcerr << (resultAt == std::chrono::steady_clock::time_point{} ? L"Ready to timeout: " : L"Ready to result: ")
                   << std::chrono::duration_cast<std::chrono::milliseconds>(
                       (resultAt == std::chrono::steady_clock::time_point{} ? std::chrono::steady_clock::now() : resultAt)-readyAt).count() << L" ms\n";
    }
    if (!result && readyAt != std::chrono::steady_clock::time_point{} && resultAt != std::chrono::steady_clock::time_point{})
      std::cout << "ReadyToResultMs=" << std::chrono::duration_cast<std::chrono::milliseconds>(resultAt-readyAt).count() << "\n";
    std::cout << (result ? "Live pipeline test failed\n" : "Live capture -> OCR -> local model -> overlay/cache passed\n");
  } catch (const std::exception& error) { result=1; std::cerr << error.what() << '\n'; }
  catch (const winrt::hresult_error& error) { result=1; std::cerr << "Live test HRESULT " << std::hex << error.code().value << '\n'; }
  winrt::uninit_apartment();
  return result;
}
