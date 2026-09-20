#include "d4r0/DebugLog.h"
#include "d4r0/OverlayWindow.h"
#include "d4r0/ReplayBuffer.h"
#include "d4r0/Settings.h"
#include "d4r0/WindowsGraphicsCapture.h"
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
  if (!store.save(settings)) { d4r0::debugLog("Settings save failed"); MessageBoxW(nullptr, L"d4r0 could not save local settings.", L"d4r0", MB_ICONERROR); return 1; }
  const auto captureMonitorIndex = settings.captureMonitorIndex;
  d4r0::RegionCache cache; d4r0::OverlayWindow overlay(std::move(settings), cache);
  if (!overlay.create(instance)) { d4r0::debugLog("Overlay creation or capture exclusion failed"); MessageBoxW(nullptr, L"d4r0 could not create its D3D11/DirectComposition overlay.", L"d4r0", MB_ICONERROR); return 1; }
  d4r0::WindowsGraphicsCapture capture(captureMonitorIndex);
  if (!capture.start()) { d4r0::debugLog("Windows Graphics Capture startup failed"); MessageBoxW(nullptr, L"d4r0 could not start Windows Graphics Capture for the configured monitor.", L"d4r0", MB_ICONERROR); return 1; }
  d4r0::ReplayBuffer replay;
  if (!replay.start()) d4r0::debugLog("Optional replay seam did not start");
  d4r0::debugLog("Startup complete");
  MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
  d4r0::debugLog("Shutting down cleanly");
  return 0;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDPIAware();
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
