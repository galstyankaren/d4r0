#include "d4r0/DebugLog.h"
#include "d4r0/OverlayWindow.h"
#include "d4r0/ReplayBuffer.h"
#include "d4r0/Settings.h"
#include "d4r0/WindowsGraphicsCapture.h"
#include "d4r0/LivePipeline.h"
#include "d4r0/TrayController.h"
#include <shlobj_core.h>
#include <winrt/base.h>
#include <memory>

namespace {
std::filesystem::path settingsPath() {
  PWSTR raw{}; if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) return L"settings.ini";
  std::filesystem::path result = std::filesystem::path(raw) / L"d4r0" / L"settings.ini"; CoTaskMemFree(raw); return result;
}
int run(HINSTANCE instance) {
  const auto localSettingsPath = settingsPath();
  d4r0::initializeDebugLog(localSettingsPath.parent_path() / L"d4r0.log");
  d4r0::debugLog("Starting d4r0");
  d4r0::SettingsStore store(localSettingsPath); auto settings = store.load();
  wchar_t executable[32768]{};
  GetModuleFileNameW(nullptr,executable,32768);
  const auto assets = std::filesystem::path(executable).parent_path().parent_path()/L"local-assets";
  auto defaultAsset = [&](std::filesystem::path& path, const std::filesystem::path& relative) {
    if (path.empty() && std::filesystem::is_regular_file(assets/relative)) path = assets/relative;
  };
  defaultAsset(settings.llamaExecutable,L"llama-b11068-vulkan/llama-server.exe");
  defaultAsset(settings.model4b,L"translategemma-4b-it.Q4_K_M.gguf");
  defaultAsset(settings.ocrDetector,L"ocr-det/inference.onnx");
  defaultAsset(settings.ocrRecognizer,L"ocr-rec/inference.onnx");
  defaultAsset(settings.ocrDictionary,L"ocr-rec/inference.yml");
  if (!store.save(settings)) { d4r0::debugLog("Settings save failed"); MessageBoxW(nullptr, L"d4r0 could not save local settings.", L"d4r0", MB_ICONERROR); return 1; }
  const auto captureMonitorIndex = settings.captureMonitorIndex;
  d4r0::RegionCache cache; d4r0::OverlayWindow overlay(settings, cache);
  if (!overlay.create(instance)) { d4r0::debugLog("Overlay creation or capture exclusion failed"); MessageBoxW(nullptr, L"d4r0 could not create its D3D11/DirectComposition overlay.", L"d4r0", MB_ICONERROR); return 1; }
  d4r0::WindowsGraphicsCapture capture(captureMonitorIndex);
  if (!capture.start()) { d4r0::debugLog("Windows Graphics Capture startup failed"); MessageBoxW(nullptr, L"d4r0 could not start Windows Graphics Capture for the configured monitor.", L"d4r0", MB_ICONERROR); return 1; }
  d4r0::TrayController tray(instance,settings,store,overlay,[&] { PostMessageW(overlay.hwnd(),WM_CLOSE,0,0); });
  if (!tray.create()) d4r0::debugLog("Taskbar tray icon could not be created");
  d4r0::ReplayBuffer replay;
  if (settings.replayEnabled && !replay.start()) d4r0::debugLog("Hardware replay did not start");
  std::jthread replayWorker;
  if (replay.active()) replayWorker = std::jthread([&](std::stop_token stop) {
    std::uint64_t revision{};
    while (!stop.stop_requested() && replay.active()) {
      const auto frame = capture.latestFrame();
      if (frame.texture && frame.revision != revision) {
        replay.submitSourceFrame(frame.texture.Get(),std::chrono::steady_clock::now()); revision = frame.revision;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  });
  d4r0::LivePipeline pipeline(settings,capture,cache,[&](std::wstring status) {
    status += replay.active() ? L" | replay HW" : (settings.replayEnabled ? L" | replay stopped" : L" | replay off");
    tray.setStatus(status);
    overlay.setStatus(std::move(status));
  });
  d4r0::debugLog("Startup complete");
  MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
  pipeline.stop();
  replayWorker.request_stop(); if (replayWorker.joinable()) replayWorker.join(); replay.stop();
  d4r0::debugLog("Shutting down cleanly");
  return 0;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  try {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
  } catch (...) {
    MessageBoxW(nullptr, L"d4r0 could not initialize the Windows Runtime.", L"d4r0", MB_ICONERROR);
    return 1;
  }
  const int result = run(instance);
  winrt::uninit_apartment();
  return result;
}
