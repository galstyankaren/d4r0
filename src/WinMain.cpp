#include "d4r0/OverlayWindow.h"
#include "d4r0/ReplayBuffer.h"
#include "d4r0/Settings.h"
#include "d4r0/WindowsGraphicsCapture.h"
#include <shlobj_core.h>
#include <memory>

namespace {
std::filesystem::path settingsPath() {
  PWSTR raw{}; if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) return L"settings.ini";
  std::filesystem::path result = std::filesystem::path(raw) / L"d4r0" / L"settings.ini"; CoTaskMemFree(raw); return result;
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
  SetProcessDPIAware();
  d4r0::SettingsStore store(settingsPath()); auto settings = store.load();
  if (!store.save(settings)) { MessageBoxW(nullptr, L"d4r0 could not save local settings.", L"d4r0", MB_ICONERROR); return 1; }
  const auto captureMonitorIndex = settings.captureMonitorIndex;
  d4r0::RegionCache cache; d4r0::OverlayWindow overlay(std::move(settings), cache);
  if (!overlay.create(instance)) { MessageBoxW(nullptr, L"d4r0 could not create its D3D11/DirectComposition overlay.", L"d4r0", MB_ICONERROR); return 1; }
  d4r0::WindowsGraphicsCapture capture(captureMonitorIndex);
  if (!capture.start()) { MessageBoxW(nullptr, L"d4r0 could not start Windows Graphics Capture for the configured monitor.", L"d4r0", MB_ICONERROR); return 1; }
  d4r0::ReplayBuffer replay;
  if (!replay.start()) { /* Replay is optional until the local encoder is implemented. */ }
  MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
  return 0;
}
