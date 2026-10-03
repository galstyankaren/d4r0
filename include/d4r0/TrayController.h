#pragma once
#include "d4r0/OverlayWindow.h"
#include "d4r0/Settings.h"
#include "d4r0/TranslationProfiles.h"
#include <shellapi.h>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
  void setProfileCallbacks(std::function<void()> profilesChanged,
                           std::function<void(ProfileDraftRequest)> requestDraft);
  void setActiveProfile(ActiveProfile active);
  void setDraftResult(ProfileDraftResult result);
  bool isDialogMessage(MSG* message);
 private:
  static LRESULT CALLBACK messageProc(HWND, UINT, WPARAM, LPARAM);
  static LRESULT CALLBACK controlProc(HWND, UINT, WPARAM, LPARAM);
  void showMenu();
  void showControl();
  void refreshControl();
  void toggleModel();
  void toggleDebug();
  void refreshShortcut();
  void beginLearning();
  void finishLearning();
  void pollControllerLearning();
  void captureHidButtons(const std::vector<std::uint32_t>& buttons);
  void handleRawInput(HRAWINPUT input);
  void setHidListening(bool enabled);
  void captureKey(WPARAM key);
  void applyShortcut();
  void useDefault();
  void showPage(int page);
  void refreshProfiles();
  void loadSelectedProfile();
  bool saveProfile();
  void createProfile();
  void requestProfileDraft();
  void layoutControls();
  void updateProfileControls();
  HINSTANCE instance_{}; PipelineSettings& settings_; SettingsStore& store_; OverlayWindow& overlay_;
  std::function<void()> exit_; HWND messageWindow_{}; HWND controlWindow_{}; HWND statusLabel_{};
  HWND actionList_{}, currentLabel_{}, previewLabel_{}, helpLabel_{};
  HWND pageTabs_[3]{};
  HWND overviewText_{}, activeProfileText_{}, profileList_{}, appNameEdit_{}, appPathEdit_{};
  HWND categoryList_{}, sourceList_{}, targetList_{}, instructionsEdit_{}, draftDescriptionEdit_{};
  HWND draftStatus_{}, createProfileButton_{}, generateDraftButton_{}, saveProfileButton_{}, cancelProfileButton_{};
  std::vector<HWND> settingsControls_, profileControls_, overviewControls_;
  TranslationProfile editingProfile_;
  ActiveProfile activeProfile_;
  std::function<void()> profilesChanged_;
  std::function<void(ProfileDraftRequest)> requestDraft_;
  mutable std::mutex profileMutex_;
  mutable std::mutex controlWindowMutex_;
  std::uint64_t nextDraftId_{1};
  std::uint64_t pendingDraftId_{};
  std::wstring pendingDraftInstructions_;
  int currentPage_{};
  int scrollOffset_{};
  HFONT controlFont_{};
  std::wstring preview_;
  std::wstring shortcutNotice_;
  bool learning_{};
  bool controllerWasDown_{};
  bool hidListening_{};
  bool hidReportSeen_{};
  ULONGLONG learningStarted_{};
  struct HidDeviceInfo {
    bool initialized{};
    std::vector<std::uint64_t> preparsed;
    std::vector<std::uint16_t> usages;
  };
  std::unordered_map<HANDLE,HidDeviceInfo> hidDevices_;
  ControllerBinding controllerPeak_;
  mutable std::mutex statusMutex_; std::wstring status_; NOTIFYICONDATAW icon_{}; bool created_{};
};
} // namespace d4r0
