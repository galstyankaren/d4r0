#include "d4r0/OverlayWindow.h"
#include "d4r0/ReplayBuffer.h"
#include "d4r0/Settings.h"
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
  d4r0::SettingsStore store(settingsPath()); auto settings = store.load(); store.save(settings);
  d4r0::RegionCache cache; d4r0::OverlayWindow overlay(std::move(settings), cache);
  if (!overlay.create(instance)) { MessageBoxW(nullptr, L"d4r0 could not create its D3D11/DirectComposition overlay.", L"d4r0", MB_ICONERROR); return 1; }
  d4r0::ReplayBuffer replay; replay.start();
  MSG message{}; while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
  return 0;
}
