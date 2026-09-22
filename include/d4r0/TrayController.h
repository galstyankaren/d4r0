#pragma once
#include "d4r0/OverlayWindow.h"
#include "d4r0/Settings.h"
#include <shellapi.h>
#include <functional>
#include <mutex>
#include <string>

namespace d4r0 {
class TrayController {
 public:
  TrayController(HINSTANCE instance, PipelineSettings& settings, SettingsStore& store,
                 OverlayWindow& overlay, std::function<void()> exit);
  ~TrayController();
  TrayController(const TrayController&) = delete;
  bool create();
  void destroy();
  void setStatus(std::wstring status);
 private:
  static LRESULT CALLBACK messageProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK controlProc(HWND, UINT, WPARAM, LPARAM);
  void showMenu();
  void showControl();
  void refreshControl();
  HINSTANCE instance_{}; PipelineSettings& settings_; SettingsStore& store_; OverlayWindow& overlay_;
  std::function<void()> exit_; HWND messageWindow_{}; HWND controlWindow_{}; HWND statusLabel_{};
  mutable std::mutex statusMutex_; std::wstring status_; NOTIFYICONDATAW icon_{}; bool created_{};
};
} // namespace d4r0
