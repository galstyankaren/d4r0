#include "d4r0/TrayController.h"
#include "d4r0/DebugLog.h"
#include <shellapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Gaming.Input.h>
#include <algorithm>
#include <commctrl.h>
#include <memory>
#include <vector>

namespace d4r0 {
namespace {
constexpr UINT kTrayMessage = WM_APP + 41;
constexpr UINT kOpen = 1, kModel = 2, kDebug = 3, kExit = 4;
constexpr int kModelButton = 101, kDebugButton = 102, kExitButton = 103;
constexpr int kActionList = 104, kLearnButton = 105, kDefaultButton = 106;
constexpr int kApplyButton = 107, kCancelButton = 108;
constexpr int kOverviewTab = 109, kProfilesTab = 110, kSettingsTab = 111;
constexpr int kProfileList = 112, kAppName = 113, kAppPath = 114, kCategory = 115;
constexpr int kSource = 116, kTarget = 117, kInstructions = 118, kDraftDescription = 119;
constexpr int kCreateProfile = 120, kGenerateDraft = 121, kSaveProfile = 122, kCancelProfile = 123;
constexpr UINT kProfileActiveMessage = WM_APP + 43;
constexpr UINT kDraftResultMessage = WM_APP + 44;
constexpr UINT_PTR kLearnTimer = 2;
const wchar_t* kMessageClass = L"d4r0.TrayMessage";
const wchar_t* kControlClass = L"d4r0.ControlWindow";
UINT taskbarCreated() { static const UINT value=RegisterWindowMessageW(L"TaskbarCreated"); return value; }
struct ActivePayload { ActiveProfile value; };
struct DraftPayload { ProfileDraftResult value; };
std::wstring profileLabel(const TranslationProfile& profile) {
  auto label = profile.displayName.empty() ? L"Unnamed profile" : profile.displayName;
  if (!profile.executablePath.empty()) label += L" - " + profile.executablePath.filename().wstring();
  return label;
}
std::string comboCode(HWND combo, const std::vector<Language>& languages) {
  const auto selected = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
  return selected >= 0 && static_cast<std::size_t>(selected) < languages.size() ? languages[selected].code : "";
}
void selectLanguage(HWND combo, const std::vector<Language>& languages, const std::string& code) {
  for (std::size_t i = 0; i < languages.size(); ++i)
    if (languages[i].code == code) { SendMessageW(combo, CB_SETCURSEL, i, 0); return; }
  if (!languages.empty()) SendMessageW(combo, CB_SETCURSEL, 0, 0);
}
std::wstring editText(HWND control) {
  const int length = GetWindowTextLengthW(control);
  std::wstring value(static_cast<std::size_t>(length)+1, L'\0');
  if (length) GetWindowTextW(control, value.data(), length + 1);
  value.resize(static_cast<std::size_t>(length));
  return value;
}
std::wstring keyName(WPARAM key) {
  if ((key >= 'A' && key <= 'Z') || (key >= '0' && key <= '9'))
    return std::wstring(1, static_cast<wchar_t>(key));
  if (key >= VK_F1 && key <= VK_F24) return L"F" + std::to_wstring(key - VK_F1 + 1);
  switch (key) {
    case VK_TAB: return L"Tab";
    case VK_SPACE: return L"Space";
    case VK_ESCAPE: return L"Esc";
    default: return L"";
  }
}
}
TrayController::TrayController(HINSTANCE instance, PipelineSettings& settings, SettingsStore& store,
                               OverlayWindow& overlay, std::function<void()> exit)
    : instance_(instance), settings_(settings), store_(store), overlay_(overlay), exit_(std::move(exit)) {}
TrayController::~TrayController() { destroy(); }
bool TrayController::create() {
  WNDCLASSW messageClass{}; messageClass.hInstance=instance_; messageClass.lpfnWndProc=messageProc; messageClass.lpszClassName=kMessageClass;
  RegisterClassW(&messageClass);
  WNDCLASSW controlClass{}; controlClass.hInstance=instance_; controlClass.lpfnWndProc=controlProc; controlClass.lpszClassName=kControlClass; controlClass.hCursor=LoadCursor(nullptr,IDC_ARROW); controlClass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
  RegisterClassW(&controlClass);
  messageWindow_=CreateWindowExW(0,kMessageClass,L"d4r0",0,0,0,0,0,HWND_MESSAGE,nullptr,instance_,this);
  if(!messageWindow_) return false;
  if (const auto binding = parseControllerBinding(settings_.toggleControllerButton)) setHidListening(binding->hid);
  icon_.cbSize=sizeof(icon_); icon_.hWnd=messageWindow_; icon_.uID=1; icon_.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; icon_.uCallbackMessage=kTrayMessage; icon_.hIcon=LoadIcon(nullptr,IDI_APPLICATION); lstrcpyW(icon_.szTip,L"d4r0 local translator");
  created_=Shell_NotifyIconW(NIM_ADD,&icon_) != FALSE;
  if(created_) Shell_NotifyIconW(NIM_SETVERSION,&icon_);
  return created_;
}
void TrayController::destroy() {
  finishLearning();
  setHidListening(false);
  {
    std::scoped_lock lock(controlWindowMutex_);
    if(controlWindow_) {
      MSG message{};
      while(PeekMessageW(&message,controlWindow_,kProfileActiveMessage,kDraftResultMessage,PM_REMOVE)) {
        if(message.message==kProfileActiveMessage) delete reinterpret_cast<ActivePayload*>(message.lParam);
        else if(message.message==kDraftResultMessage) delete reinterpret_cast<DraftPayload*>(message.lParam);
      }
      DestroyWindow(controlWindow_); controlWindow_=nullptr;
    }
  }
  if(controlFont_) { DeleteObject(controlFont_); controlFont_=nullptr; }
  if(created_) { Shell_NotifyIconW(NIM_DELETE,&icon_); created_=false; }
  if(messageWindow_) { DestroyWindow(messageWindow_); messageWindow_=nullptr; }
}
void TrayController::setStatus(std::wstring status) {
  { std::scoped_lock lock(statusMutex_); status_=std::move(status); }
  std::scoped_lock lock(controlWindowMutex_);
  if(controlWindow_) PostMessageW(controlWindow_,WM_APP+42,0,0);
}
void TrayController::toggleModel() {
  settings_.selectedModel = settings_.selectedModel == ModelChoice::TranslateGemma4B ?
      ModelChoice::TranslateGemma12B : ModelChoice::TranslateGemma4B;
  store_.save(settings_);
  refreshControl();
}
void TrayController::toggleDebug() {
  settings_.showDiagnostics = !overlay_.diagnosticsEnabled();
  overlay_.setDiagnostics(settings_.showDiagnostics);
  store_.save(settings_);
  refreshControl();
}
void TrayController::showMenu() {
  HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,kOpen,L"Open controls"); AppendMenuW(menu,MF_STRING,kModel,settings_.selectedModel==ModelChoice::TranslateGemma4B?L"Model: 4B (restart applies)":L"Model: 12B (restart applies)"); AppendMenuW(menu,MF_STRING| (overlay_.diagnosticsEnabled()?MF_CHECKED:0),kDebug,L"Show debug overlay"); AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,kExit,L"Exit d4r0");
  POINT point{}; GetCursorPos(&point); SetForegroundWindow(messageWindow_); const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,messageWindow_,nullptr); DestroyMenu(menu);
  if(command==kOpen) showControl(); else if(command==kModel) { toggleModel(); showControl(); } else if(command==kDebug) { toggleDebug(); showControl(); } else if(command==kExit) exit_();
}
void TrayController::showControl() {
  if(!controlWindow_) {
    const auto dpi = GetDpiForSystem();
    { std::scoped_lock lock(controlWindowMutex_); controlWindow_=CreateWindowExW(WS_EX_CONTROLPARENT,kControlClass,L"d4r0 controls",
        WS_OVERLAPPEDWINDOW|WS_VSCROLL,CW_USEDEFAULT,CW_USEDEFAULT,MulDiv(900,dpi,96),MulDiv(760,dpi,96),
        nullptr,nullptr,instance_,this); }
    if(!controlWindow_) return;
    controlFont_=CreateFontW(-MulDiv(16,dpi,96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,
                             CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    auto add=[&](const wchar_t* type,const wchar_t* label,DWORD style,int id=0) {
      HWND child=CreateWindowW(type,label,WS_CHILD|WS_VISIBLE|style,0,0,0,0,controlWindow_,
          id ? reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)) : nullptr,instance_,nullptr);
      SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(controlFont_),TRUE);
      return child;
    };
    pageTabs_[0]=add(L"BUTTON",L"Overview",BS_PUSHBUTTON|WS_TABSTOP,kOverviewTab);
    pageTabs_[1]=add(L"BUTTON",L"Profiles",BS_PUSHBUTTON|WS_TABSTOP,kProfilesTab);
    pageTabs_[2]=add(L"BUTTON",L"Settings",BS_PUSHBUTTON|WS_TABSTOP,kSettingsTab);
    overviewText_=add(L"STATIC",L"d4r0 translates recognized game text locally.",SS_LEFT);
    activeProfileText_=add(L"STATIC",L"",SS_LEFT);
    overviewControls_={overviewText_,activeProfileText_};

    profileList_=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|CBS_SORT|WS_TABSTOP,kProfileList);
    createProfileButton_=add(L"BUTTON",L"Create app profile",BS_PUSHBUTTON|WS_TABSTOP,kCreateProfile);
    std::vector<HWND> profileLabels;
    auto label=[&](const wchar_t* text) { auto control=add(L"STATIC",text,SS_LEFT); profileLabels.push_back(control); return control; };
    label(L"Application name"); appNameEdit_=add(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,kAppName);
    label(L"Executable path"); appPathEdit_=add(L"EDIT",L"",ES_AUTOHSCROLL|WS_TABSTOP,kAppPath);
    label(L"Category"); categoryList_=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_TABSTOP,kCategory);
    for (auto kind : {ProfileTemplate::Game,ProfileTemplate::Browser,ProfileTemplate::Professional,ProfileTemplate::General})
      SendMessageW(categoryList_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(templateName(kind).c_str()));
    label(L"Source language"); sourceList_=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,kSource);
    label(L"Target language"); targetList_=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,kTarget);
    const auto sources=sourceLanguages(), targets=targetLanguages();
    for (const auto& language : sources) SendMessageW(sourceList_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(language.name.c_str()));
    for (const auto& language : targets) SendMessageW(targetList_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(language.name.c_str()));
    label(L"Additional translation instructions");
    instructionsEdit_=add(L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP|WS_BORDER,kInstructions);
    label(L"Optional description for a generated draft");
    draftDescriptionEdit_=add(L"EDIT",L"",ES_MULTILINE|ES_AUTOVSCROLL|WS_VSCROLL|WS_TABSTOP|WS_BORDER,kDraftDescription);
    draftStatus_=add(L"STATIC",L"Generate creates an editable local draft. Save applies profile changes.",SS_LEFT);
    generateDraftButton_=add(L"BUTTON",L"Generate draft",BS_PUSHBUTTON|WS_TABSTOP,kGenerateDraft);
    saveProfileButton_=add(L"BUTTON",L"Save profile",BS_DEFPUSHBUTTON|WS_TABSTOP,kSaveProfile);
    cancelProfileButton_=add(L"BUTTON",L"Cancel edits",BS_PUSHBUTTON|WS_TABSTOP,kCancelProfile);
    profileControls_={profileList_,createProfileButton_,appNameEdit_,appPathEdit_,categoryList_,sourceList_,targetList_,
                      instructionsEdit_,draftDescriptionEdit_,draftStatus_,generateDraftButton_,saveProfileButton_,cancelProfileButton_};
    profileControls_.insert(profileControls_.end(),profileLabels.begin(),profileLabels.end());

    auto settingsLabel=add(L"STATIC",L"Shortcuts (keyboard and HID controller)",SS_LEFT);
    auto help=add(L"STATIC",L"Learn a shortcut, review its preview, then apply it.",SS_LEFT);
    actionList_=add(L"COMBOBOX",L"",CBS_DROPDOWNLIST|WS_VSCROLL|WS_TABSTOP,kActionList);
    for (const wchar_t* item : {L"Toggle translation - keyboard",L"Show original - keyboard",
                                L"Toggle diagnostics - keyboard",L"Exit d4r0 - keyboard",
                                L"Toggle translation - controller"})
      SendMessageW(actionList_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(item));
    SendMessageW(actionList_,CB_SETCURSEL,0,0);
    currentLabel_=add(L"STATIC",L"",SS_LEFT);
    previewLabel_=add(L"STATIC",L"",SS_LEFT);
    helpLabel_=add(L"STATIC",L"",SS_LEFT);
    auto learn=add(L"BUTTON",L"Learn new shortcut",BS_PUSHBUTTON|WS_TABSTOP,kLearnButton);
    auto defaults=add(L"BUTTON",L"Use default",BS_PUSHBUTTON|WS_TABSTOP,kDefaultButton);
    auto apply=add(L"BUTTON",L"Apply preview",BS_PUSHBUTTON|WS_TABSTOP,kApplyButton);
    auto cancel=add(L"BUTTON",L"Cancel",BS_PUSHBUTTON|WS_TABSTOP,kCancelButton);
    auto model=add(L"BUTTON",L"Switch model (restart)",BS_PUSHBUTTON|WS_TABSTOP,kModelButton);
    auto debug=add(L"BUTTON",L"Toggle debug overlay",BS_PUSHBUTTON|WS_TABSTOP,kDebugButton);
    auto exit=add(L"BUTTON",L"Exit",BS_PUSHBUTTON|WS_TABSTOP,kExitButton);
    statusLabel_=add(L"STATIC",L"",SS_LEFT);
    settingsControls_={settingsLabel,help,actionList_,currentLabel_,previewLabel_,helpLabel_,learn,defaults,apply,cancel,model,debug,exit,statusLabel_};
    refreshProfiles();
    showPage(0);
    layoutControls();
    SetFocus(pageTabs_[0]);
  }
  refreshShortcut(); refreshControl(); refreshProfiles(); ShowWindow(controlWindow_,SW_SHOW); SetForegroundWindow(controlWindow_);
}
void TrayController::refreshControl() {
  if(!statusLabel_) return;
  std::wstring status; { std::scoped_lock lock(statusMutex_); status=status_; }
  std::wstring text=L"Model: "; text += settings_.selectedModel==ModelChoice::TranslateGemma4B?L"4B":L"12B";
  text += overlay_.diagnosticsEnabled()?L" | Debug: on | ":L" | Debug: off | ";
  text += status; SetWindowTextW(statusLabel_,text.c_str());
  ActiveProfile active; { std::scoped_lock lock(profileMutex_); active=activeProfile_; }
  SetWindowTextW(activeProfileText_,(L"Active app: " + active.app.displayName + L"    Profile: " +
      profileLabel(active.profile) + (active.savedMatch ? L" (saved app match)" : L" (suggested)" )).c_str());
}
void TrayController::setProfileCallbacks(std::function<void()> profilesChanged,
                                         std::function<void(ProfileDraftRequest)> requestDraft) {
  profilesChanged_ = std::move(profilesChanged);
  requestDraft_ = std::move(requestDraft);
}
void TrayController::setActiveProfile(ActiveProfile active) {
  std::scoped_lock windowLock(controlWindowMutex_);
  if (!controlWindow_) { std::scoped_lock lock(profileMutex_); activeProfile_ = std::move(active); return; }
  auto* payload = new ActivePayload{std::move(active)};
  if (!PostMessageW(controlWindow_,kProfileActiveMessage,0,reinterpret_cast<LPARAM>(payload))) delete payload;
}
void TrayController::setDraftResult(ProfileDraftResult result) {
  std::scoped_lock lock(controlWindowMutex_);
  if (!controlWindow_) return;
  auto* payload = new DraftPayload{std::move(result)};
  if (!PostMessageW(controlWindow_,kDraftResultMessage,0,reinterpret_cast<LPARAM>(payload))) delete payload;
}
bool TrayController::isDialogMessage(MSG* message) {
  return controlWindow_ && IsWindowVisible(controlWindow_) && message && IsDialogMessageW(controlWindow_,message);
}
void TrayController::showPage(int page) {
  currentPage_ = std::clamp(page,0,2);
  auto setVisible=[&](const std::vector<HWND>& controls, bool visible) {
    for (const auto control : controls) ShowWindow(control,visible?SW_SHOW:SW_HIDE);
  };
  setVisible(overviewControls_,currentPage_==0);
  setVisible(profileControls_,currentPage_==1);
  setVisible(settingsControls_,currentPage_==2);
  for (int i=0;i<3;++i) EnableWindow(pageTabs_[i],i!=currentPage_);
  scrollOffset_=0;
  SetScrollPos(controlWindow_,SB_VERT,0,TRUE);
  layoutControls();
}
void TrayController::refreshProfiles() {
  if (!profileList_) return;
  const auto previous = editingProfile_.id;
  SendMessageW(profileList_,CB_RESETCONTENT,0,0);
  int selected=-1;
  for (std::size_t i=0;i<settings_.profiles.size();++i) {
    const auto label=profileLabel(settings_.profiles[i]);
    const auto row=static_cast<int>(SendMessageW(profileList_,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str())));
    SendMessageW(profileList_,CB_SETITEMDATA,row,static_cast<LPARAM>(i));
    if (settings_.profiles[i].id==previous) selected=row;
  }
  if (selected<0 && SendMessageW(profileList_,CB_GETCOUNT,0,0)>0) selected=0;
  SendMessageW(profileList_,CB_SETCURSEL,selected,0);
  if (selected>=0) loadSelectedProfile();
  else updateProfileControls();
}
void TrayController::loadSelectedProfile() {
  if (!profileList_) return;
  const int row=static_cast<int>(SendMessageW(profileList_,CB_GETCURSEL,0,0));
  if (row<0) return;
  const auto index=static_cast<std::size_t>(SendMessageW(profileList_,CB_GETITEMDATA,row,0));
  if (index>=settings_.profiles.size()) return;
  editingProfile_=settings_.profiles[index];
  pendingDraftId_=0;
  SetWindowTextW(draftStatus_,L"Generate creates an editable local draft. Save applies profile changes.");
  updateProfileControls();
}
void TrayController::updateProfileControls() {
  if (!appNameEdit_) return;
  const bool builtInTemplate = editingProfile_.id.starts_with("template-");
  EnableWindow(appPathEdit_, !builtInTemplate);
  EnableWindow(categoryList_, !builtInTemplate);
  SetWindowTextW(appNameEdit_,editingProfile_.displayName.c_str());
  SetWindowTextW(appPathEdit_,editingProfile_.executablePath.c_str());
  const auto templateIndex=static_cast<int>(editingProfile_.templateKind);
  SendMessageW(categoryList_,CB_SETCURSEL,templateIndex,0);
  selectLanguage(sourceList_,sourceLanguages(),editingProfile_.sourceLanguage);
  selectLanguage(targetList_,targetLanguages(),editingProfile_.targetLanguage);
  SetWindowTextW(instructionsEdit_,editingProfile_.additionalInstructions.c_str());
}
bool TrayController::saveProfile() {
  TranslationProfile candidateProfile=editingProfile_;
  candidateProfile.displayName=editText(appNameEdit_);
  candidateProfile.executablePath=editText(appPathEdit_);
  const auto category=static_cast<int>(SendMessageW(categoryList_,CB_GETCURSEL,0,0));
  if (category>=0 && category<=static_cast<int>(ProfileTemplate::General))
    candidateProfile.templateKind=static_cast<ProfileTemplate>(category);
  candidateProfile.sourceLanguage=comboCode(sourceList_,sourceLanguages());
  candidateProfile.targetLanguage=comboCode(targetList_,targetLanguages());
  candidateProfile.additionalInstructions=editText(instructionsEdit_);
  if (editingProfile_.id.empty() && candidateProfile.executablePath.empty()) {
    MessageBoxW(controlWindow_,L"Choose an executable path for this app profile.",L"d4r0",MB_ICONWARNING);
    return false;
  }
  if (candidateProfile.id.empty()) {
    candidateProfile.id=candidateProfile.executablePath.empty() ?
        std::to_string(nextDraftId_++) : candidateProfile.executablePath.string();
  }
  if (const auto error=validateProfile(candidateProfile)) {
    MessageBoxW(controlWindow_,L"Check the app name, executable path, category, and language selections.",L"d4r0",MB_ICONWARNING);
    return false;
  }
  auto candidate=settings_;
  if (!candidateProfile.executablePath.empty() &&
      std::any_of(candidate.profiles.begin(),candidate.profiles.end(),[&](const auto& profile) {
        return profile.id != candidateProfile.id &&
            sameExecutablePath(profile.executablePath,candidateProfile.executablePath);
      })) {
    MessageBoxW(controlWindow_,L"This executable already has a saved profile. Select and edit that profile.",L"d4r0",MB_ICONWARNING);
    return false;
  }
  auto found=std::find_if(candidate.profiles.begin(),candidate.profiles.end(),[&](const auto& profile){return profile.id==candidateProfile.id;});
  if (found==candidate.profiles.end()) candidate.profiles.push_back(candidateProfile);
  else *found=candidateProfile;
  if (!store_.save(candidate)) {
    MessageBoxW(controlWindow_,L"Could not save settings.",L"d4r0",MB_ICONERROR);
    return false;
  }
  settings_=std::move(candidate);
  editingProfile_=std::move(candidateProfile);
  pendingDraftId_=0;
  {
    std::scoped_lock lock(profileMutex_);
    activeProfile_=profileForApp(settings_.profiles,activeProfile_.app);
  }
  refreshProfiles();
  refreshControl();
  SetWindowTextW(draftStatus_,L"Profile saved and active for matching apps.");
  if (profilesChanged_) profilesChanged_();
  return true;
}
void TrayController::createProfile() {
  finishLearning();
  ActiveProfile active; { std::scoped_lock lock(profileMutex_); active=activeProfile_; }
  editingProfile_=active.profile;
  editingProfile_.id.clear();
  editingProfile_.displayName=active.app.displayName;
  editingProfile_.executablePath=active.app.executablePath;
  SetWindowTextW(draftDescriptionEdit_,L"");
  pendingDraftId_=0;
  SetWindowTextW(draftStatus_,L"New profile. Enter its app details, then Save profile.");
  updateProfileControls();
  SetFocus(appNameEdit_);
}
void TrayController::requestProfileDraft() {
  if (!requestDraft_) {
    SetWindowTextW(draftStatus_,L"Draft generation is unavailable.");
    return;
  }
  ProfileDraftRequest request;
  request.requestId=nextDraftId_++;
  pendingDraftId_=request.requestId;
  request.appName=editText(appNameEdit_);
  request.description=editText(draftDescriptionEdit_);
  const auto category=static_cast<int>(SendMessageW(categoryList_,CB_GETCURSEL,0,0));
  if (category>=0 && category<=static_cast<int>(ProfileTemplate::General))
    request.templateKind=static_cast<ProfileTemplate>(category);
  request.sourceLanguage=comboCode(sourceList_,sourceLanguages());
  request.targetLanguage=comboCode(targetList_,targetLanguages());
  pendingDraftInstructions_=editText(instructionsEdit_);
  SetWindowTextW(draftStatus_,L"Generating a local draft…");
  requestDraft_(std::move(request));
}
void TrayController::layoutControls() {
  if (!controlWindow_) return;
  const auto dpi=GetDpiForWindow(controlWindow_);
  const auto s=[&](int value){return MulDiv(value,dpi,96);};
  RECT client{}; GetClientRect(controlWindow_,&client);
  const int width=std::max(480,MulDiv(static_cast<int>(client.right),96,dpi));
  const int margin=24, gap=12, full=width-2*margin;
  auto place=[&](HWND hwnd,int x,int y,int w,int h){if(hwnd) SetWindowPos(hwnd,nullptr,s(x),s(y-scrollOffset_),s(w),s(h),SWP_NOZORDER|SWP_NOACTIVATE);};
  for (int i=0;i<3;++i) place(pageTabs_[i],24+i*112,16+scrollOffset_,104,38);
  const int y0=72;
  if (currentPage_==0) {
    place(overviewText_,24,y0,full,34);
    place(activeProfileText_,24,y0+48,full,48);
  } else if (currentPage_==1) {
    place(profileList_,24,y0,full-190,34); place(createProfileButton_,width-24-178,y0,178,34);
    const int left=24, col=std::max(200,(full-gap)/2), right=left+col+gap;
    auto labelFor=[&](int index){return profileControls_.size()>13?profileControls_[13+index]:nullptr;};
    place(labelFor(0),left,y0+50,col,24); place(appNameEdit_,left,y0+76,col,34);
    place(labelFor(1),right,y0+50,col,24); place(appPathEdit_,right,y0+76,col,34);
    place(labelFor(2),left,y0+122,col,24); place(categoryList_,left,y0+148,col,34);
    place(labelFor(3),right,y0+122,col,24); place(sourceList_,right,y0+148,col,34);
    place(labelFor(4),left,y0+194,col,24); place(targetList_,left,y0+220,col,34);
    place(labelFor(5),left,y0+270,full,24); place(instructionsEdit_,left,y0+296,full,160);
    place(labelFor(6),left,y0+466,full,24); place(draftDescriptionEdit_,left,y0+492,full,66);
    place(generateDraftButton_,left,y0+570,150,38); place(saveProfileButton_,left+162,y0+570,150,38);
    place(cancelProfileButton_,left+324,y0+570,150,38); place(draftStatus_,left,y0+620,full,42);
  } else {
    const int left=24;
    place(settingsControls_[0],left,y0,full,28); place(settingsControls_[1],left,y0+34,full,32);
    place(actionList_,left,y0+78,full,34); place(currentLabel_,left,y0+126,full,30);
    place(previewLabel_,left,y0+162,full,30); place(helpLabel_,left,y0+198,full,44);
    place(settingsControls_[6],left,y0+254,170,38); place(settingsControls_[7],left+182,y0+254,130,38);
    place(settingsControls_[8],left+324,y0+254,150,38); place(settingsControls_[9],left+486,y0+254,120,38);
    place(settingsControls_[10],left,y0+316,210,38); place(settingsControls_[11],left+222,y0+316,210,38);
    place(settingsControls_[12],left+444,y0+316,120,38); place(statusLabel_,left,y0+370,full,60);
  }
  const int contentHeight=currentPage_==1?760:520;
  SCROLLINFO info{sizeof(info),SIF_RANGE|SIF_PAGE|SIF_POS,0,contentHeight,static_cast<UINT>(MulDiv(static_cast<int>(client.bottom),96,dpi)),scrollOffset_};
  SetScrollInfo(controlWindow_,SB_VERT,&info,TRUE);
}
void TrayController::refreshShortcut() {
  if (!actionList_) return;
  const int action = static_cast<int>(SendMessageW(actionList_,CB_GETCURSEL,0,0));
  const std::wstring* current = nullptr;
  switch (action) {
    case 0: current = &settings_.toggleShortcut; break;
    case 1: current = &settings_.originalShortcut; break;
    case 2: current = &settings_.diagnosticsShortcut; break;
    case 3: current = &settings_.exitShortcut; break;
    case 4: current = &settings_.toggleControllerButton; break;
  }
  if (!current) return;
  SetWindowTextW(currentLabel_,(L"Current: " + *current).c_str());
  SetWindowTextW(previewLabel_,(L"Preview: " + (preview_.empty() ? L"—" : preview_)).c_str());
  SetWindowTextW(helpLabel_, !shortcutNotice_.empty() ? shortcutNotice_.c_str() : learning_ ?
      (action == 4 ? L"Hold all controller buttons together, then release. Review the preview before applying."
                   : L"Press Ctrl, Shift or Alt with a key. Review the preview before applying.") :
      L"Learn captures a new combination. Apply changes it immediately; Cancel keeps the current one.");
}
void TrayController::beginLearning() {
  finishLearning();
  shortcutNotice_.clear();
  preview_.clear(); controllerPeak_ = {}; controllerWasDown_ = false;
  hidReportSeen_ = false; learningStarted_ = GetTickCount64();
  learning_ = true;
  overlay_.setShortcutLearning(true);
  if (SendMessageW(actionList_,CB_GETCURSEL,0,0) == 4) {
    setHidListening(true);
    SetTimer(controlWindow_,kLearnTimer,50,nullptr);
  }
  SetFocus(controlWindow_);
  refreshShortcut();
}
void TrayController::finishLearning() {
  if (!learning_) return;
  KillTimer(controlWindow_,kLearnTimer);
  learning_ = false;
  overlay_.setShortcutLearning(false);
  const auto binding = parseControllerBinding(settings_.toggleControllerButton);
  if (!binding || !binding->hid) setHidListening(false);
  refreshShortcut();
}
void TrayController::captureKey(WPARAM key) {
  if (!learning_ || SendMessageW(actionList_,CB_GETCURSEL,0,0) == 4) return;
  const auto name = keyName(key);
  if (name.empty()) return;
  std::wstring candidate;
  if (GetKeyState(VK_CONTROL) & 0x8000) candidate += L"Ctrl+";
  if (GetKeyState(VK_SHIFT) & 0x8000) candidate += L"Shift+";
  if (GetKeyState(VK_MENU) & 0x8000) candidate += L"Alt+";
  candidate += name;
  if (!parseShortcut(candidate)) return;
  preview_ = std::move(candidate);
  refreshShortcut();
}
void TrayController::pollControllerLearning() {
  if (!learning_ || SendMessageW(actionList_,CB_GETCURSEL,0,0) != 4) return;
  ControllerBinding reading;
  try {
    using namespace winrt::Windows::Gaming::Input;
    for (const auto& gamepad : Gamepad::Gamepads()) {
      const auto state = gamepad.GetCurrentReading();
      auto buttons = static_cast<std::uint32_t>(state.Buttons) & 0x3ffff;
      if (state.LeftTrigger >= 0.6) buttons |= 0x40000;
      if (state.RightTrigger >= 0.6) buttons |= 0x80000;
      if (buttons) { reading.buttons = buttons; break; }
    }
    if (!reading.buttons) for (const auto& controller : RawGameController::RawGameControllers()) {
      const auto count = controller.ButtonCount();
      if (count <= 0) continue;
      auto buttons = std::make_unique<bool[]>(count);
      std::vector<GameControllerSwitchPosition> switches(controller.SwitchCount());
      std::vector<double> axes(controller.AxisCount());
      controller.GetCurrentReading(winrt::array_view<bool>(buttons.get(),count),switches,axes);
      for (int index = 0; index < count && index < 256; ++index)
        if (buttons[index]) reading.rawButtons.push_back(index);
      if (!reading.rawButtons.empty()) { reading.raw = true; break; }
    }
  } catch (const winrt::hresult_error&) { return; }
  const bool down = reading.raw ? !reading.rawButtons.empty() : reading.buttons != 0;
  if (down) {
    shortcutNotice_.clear();
    if (!controllerWasDown_) controllerPeak_ = reading;
    else if (controllerPeak_.raw == reading.raw) {
      controllerPeak_.buttons |= reading.buttons;
      for (const auto index : reading.rawButtons)
        if (std::find(controllerPeak_.rawButtons.begin(),controllerPeak_.rawButtons.end(),index) == controllerPeak_.rawButtons.end())
          controllerPeak_.rawButtons.push_back(index);
      std::sort(controllerPeak_.rawButtons.begin(),controllerPeak_.rawButtons.end());
    }
    preview_ = controllerBindingName(controllerPeak_);
    refreshShortcut();
  } else if (controllerWasDown_ && !preview_.empty()) {
    finishLearning();
  }
  controllerWasDown_ = down;
  if (!down && preview_.empty() && shortcutNotice_.empty() && GetTickCount64()-learningStarted_ > 2000) {
    shortcutNotice_ = hidReportSeen_ ? L"Controller connected. Press and hold the buttons you want."
                                     : L"No controller input yet. Check the connection and press a button.";
    refreshShortcut();
  }
}
void TrayController::captureHidButtons(const std::vector<std::uint32_t>& buttons) {
  if (!learning_ || SendMessageW(actionList_,CB_GETCURSEL,0,0) != 4) return;
  if (!buttons.empty()) {
    if (!controllerWasDown_) {
      controllerPeak_ = {};
      controllerPeak_.hid = true;
    }
    if (controllerPeak_.hid) {
      bool changed = !controllerWasDown_;
      for (const auto button : buttons)
        if (std::find(controllerPeak_.rawButtons.begin(),controllerPeak_.rawButtons.end(),button) == controllerPeak_.rawButtons.end())
          { controllerPeak_.rawButtons.push_back(button); changed = true; }
      if (changed) {
        shortcutNotice_.clear();
        std::sort(controllerPeak_.rawButtons.begin(),controllerPeak_.rawButtons.end());
        preview_ = controllerBindingName(controllerPeak_);
        refreshShortcut();
      }
    }
    controllerWasDown_ = true;
  } else if (controllerWasDown_ && controllerPeak_.hid) {
    controllerWasDown_ = false;
    finishLearning();
  }
}
void TrayController::setHidListening(bool enabled) {
  if (!messageWindow_ || hidListening_ == enabled) return;
  RAWINPUTDEVICE devices[2]{};
  for (auto& device : devices) {
    device.usUsagePage = 0x01;
    device.dwFlags = enabled ? RIDEV_INPUTSINK | RIDEV_DEVNOTIFY : RIDEV_REMOVE;
    device.hwndTarget = enabled ? messageWindow_ : nullptr;
  }
  devices[0].usUsage = 0x04;
  devices[1].usUsage = 0x05;
  if (RegisterRawInputDevices(devices,2,sizeof(RAWINPUTDEVICE))) hidListening_ = enabled;
  else debugLog("Could not change HID controller input registration: " + std::to_string(GetLastError()));
  if (!enabled) hidDevices_.clear();
}
void TrayController::handleRawInput(HRAWINPUT input) {
  if (!learning_ && !hidListening_) return;
  UINT size{};
  if (GetRawInputData(input,RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER)) != 0 || !size) return;
  std::vector<std::uint64_t> storage((size+7)/8);
  if (GetRawInputData(input,RID_INPUT,storage.data(),&size,sizeof(RAWINPUTHEADER)) != size) return;
  auto* raw = reinterpret_cast<RAWINPUT*>(storage.data());
  if (raw->header.dwType != RIM_TYPEHID || !raw->data.hid.dwSizeHid || !raw->data.hid.dwCount) return;
  constexpr auto headerBytes = offsetof(RAWINPUT,data.hid.bRawData);
  if (size < headerBytes ||
      static_cast<std::size_t>(raw->data.hid.dwSizeHid) * raw->data.hid.dwCount > size-headerBytes) return;
  hidReportSeen_ = true;
  auto& device = hidDevices_[raw->header.hDevice];
  if (!device.initialized) {
    UINT preparsedSize{};
    if (GetRawInputDeviceInfoW(raw->header.hDevice,RIDI_PREPARSEDDATA,nullptr,&preparsedSize) == UINT(-1) ||
        !preparsedSize) return;
    device.preparsed.resize((preparsedSize+7)/8);
    if (GetRawInputDeviceInfoW(raw->header.hDevice,RIDI_PREPARSEDDATA,device.preparsed.data(),&preparsedSize) == UINT(-1))
      return;
    auto* data = reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsed.data());
    HIDP_CAPS caps{};
    if (HidP_GetCaps(data,&caps) != HIDP_STATUS_SUCCESS) return;
    device.usages.resize(std::min<ULONG>(HidP_MaxUsageListLength(HidP_Input,0x09,data),256));
    device.initialized = true;
  }
  if (device.usages.empty()) return;
  auto* data = reinterpret_cast<PHIDP_PREPARSED_DATA>(device.preparsed.data());
  for (DWORD report = 0; report < raw->data.hid.dwCount; ++report) {
    ULONG count = static_cast<ULONG>(device.usages.size());
    const auto* bytes = reinterpret_cast<const char*>(raw->data.hid.bRawData + report*raw->data.hid.dwSizeHid);
    const auto status = HidP_GetUsages(HidP_Input,0x09,0,device.usages.data(),&count,data,
                                      const_cast<char*>(bytes),raw->data.hid.dwSizeHid);
    if (status != HIDP_STATUS_SUCCESS) continue;
    std::vector<std::uint32_t> buttons;
    for (ULONG index = 0; index < count; ++index) buttons.push_back(device.usages[index]);
    if (learning_) captureHidButtons(buttons);
    else overlay_.onHidButtons(buttons);
  }
}
void TrayController::useDefault() {
  finishLearning();
  shortcutNotice_.clear();
  PipelineSettings defaults;
  switch (SendMessageW(actionList_,CB_GETCURSEL,0,0)) {
    case 0: preview_ = defaults.toggleShortcut; break;
    case 1: preview_ = defaults.originalShortcut; break;
    case 2: preview_ = defaults.diagnosticsShortcut; break;
    case 3: preview_ = defaults.exitShortcut; break;
    case 4: preview_ = defaults.toggleControllerButton; break;
  }
  refreshShortcut();
}
void TrayController::applyShortcut() {
  if (preview_.empty()) {
    MessageBoxW(controlWindow_,L"Learn a shortcut or choose Use default first.",L"d4r0",MB_ICONINFORMATION);
    return;
  }
  finishLearning();
  const int action = static_cast<int>(SendMessageW(actionList_,CB_GETCURSEL,0,0));
  auto candidate = settings_;
  std::wstring* target = nullptr;
  int hotkey = 0;
  switch (action) {
    case 0: target = &candidate.toggleShortcut; hotkey = 1; break;
    case 1: target = &candidate.originalShortcut; hotkey = 2; break;
    case 2: target = &candidate.diagnosticsShortcut; hotkey = 4; break;
    case 3: target = &candidate.exitShortcut; hotkey = 3; break;
    case 4: target = &candidate.toggleControllerButton; break;
  }
  if (!target) return;
  *target = preview_;
  if (const auto error = validateShortcuts(candidate)) {
    MessageBoxW(controlWindow_,L"That shortcut is invalid or already assigned.",L"d4r0",MB_ICONWARNING);
    return;
  }
  const auto old = settings_;
  if (hotkey && !overlay_.updateShortcut(hotkey,preview_)) {
    MessageBoxW(controlWindow_,L"Windows could not register that shortcut. It may be in use.",L"d4r0",MB_ICONWARNING);
    return;
  }
  settings_ = candidate;
  if (!store_.save(settings_)) {
    settings_ = old;
    if (hotkey == 1) overlay_.updateShortcut(hotkey,old.toggleShortcut);
    else if (hotkey == 2) overlay_.updateShortcut(hotkey,old.originalShortcut);
    else if (hotkey == 3) overlay_.updateShortcut(hotkey,old.exitShortcut);
    else if (hotkey == 4) overlay_.updateShortcut(hotkey,old.diagnosticsShortcut);
    MessageBoxW(controlWindow_,L"Could not save settings.",L"d4r0",MB_ICONERROR);
    return;
  }
  if (action == 4) overlay_.setControllerBinding(preview_);
  if (action == 4) setHidListening(parseControllerBinding(preview_)->hid);
  if (action == 4) debugLog("Controller shortcut saved");
  preview_.clear();
  shortcutNotice_ = L"Saved and active. Press the combination to test it.";
  refreshShortcut();
}
LRESULT CALLBACK TrayController::messageProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
  auto* self=reinterpret_cast<TrayController*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if(message==WM_NCCREATE) { self=static_cast<TrayController*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
  if(self && message==kTrayMessage && (lParam==WM_LBUTTONUP || lParam==WM_RBUTTONUP || lParam==WM_CONTEXTMENU)) { self->showMenu(); return 0; }
  if(self && message==WM_INPUT) { self->handleRawInput(reinterpret_cast<HRAWINPUT>(lParam)); return DefWindowProcW(hwnd,message,wParam,lParam); }
  if(self && message==WM_INPUT_DEVICE_CHANGE && wParam==GIDC_REMOVAL) {
    self->hidDevices_.erase(reinterpret_cast<HANDLE>(lParam)); return 0;
  }
  if(self && message==taskbarCreated()) { Shell_NotifyIconW(NIM_ADD,&self->icon_); return 0; }
  return DefWindowProcW(hwnd,message,wParam,lParam);
}
LRESULT CALLBACK TrayController::controlProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
  auto* self=reinterpret_cast<TrayController*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if(message==WM_NCCREATE) { self=static_cast<TrayController*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
  if(self && message==WM_SIZE) { self->layoutControls(); return 0; }
  if(self && message==WM_GETMINMAXINFO) {
    auto* limits=reinterpret_cast<MINMAXINFO*>(lParam);
    const auto dpi=GetDpiForWindow(hwnd);
    limits->ptMinTrackSize.x=MulDiv(720,dpi,96);
    limits->ptMinTrackSize.y=MulDiv(520,dpi,96);
    return 0;
  }
  if(self && message==WM_VSCROLL) {
    SCROLLINFO info{sizeof(info),SIF_ALL}; GetScrollInfo(hwnd,SB_VERT,&info);
    int next=self->scrollOffset_;
    switch(LOWORD(wParam)) {
      case SB_LINEUP: next-=32; break; case SB_LINEDOWN: next+=32; break;
      case SB_PAGEUP: next-=static_cast<int>(info.nPage); break; case SB_PAGEDOWN: next+=static_cast<int>(info.nPage); break;
      case SB_THUMBTRACK: case SB_THUMBPOSITION: next=info.nTrackPos; break;
      case SB_TOP: next=0; break; case SB_BOTTOM: next=info.nMax; break;
    }
    const int page=self->currentPage_==1?760:520;
    RECT client{}; GetClientRect(hwnd,&client);
    const int visible=MulDiv(client.bottom,96,GetDpiForWindow(hwnd));
    self->scrollOffset_=std::clamp(next,0,std::max(0,page-visible));
    self->layoutControls(); return 0;
  }
  if(self && message==WM_MOUSEWHEEL) {
    const int delta=GET_WHEEL_DELTA_WPARAM(wParam);
    SendMessageW(hwnd,WM_VSCROLL,MAKEWPARAM(delta>0?SB_LINEUP:SB_LINEDOWN,0),0); return 0;
  }
  if(self && message==WM_DPICHANGED) {
    const auto* suggested=reinterpret_cast<RECT*>(lParam);
    SetWindowPos(hwnd,nullptr,suggested->left,suggested->top,suggested->right-suggested->left,
                 suggested->bottom-suggested->top,SWP_NOZORDER|SWP_NOACTIVATE);
    const auto oldFont=self->controlFont_;
    self->controlFont_=CreateFontW(-MulDiv(16,HIWORD(wParam),96),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,
        DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    for(HWND child=GetWindow(hwnd,GW_CHILD);child;child=GetWindow(child,GW_HWNDNEXT))
      SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(self->controlFont_),TRUE);
    if(oldFont) DeleteObject(oldFont);
    self->layoutControls(); return 0;
  }
  if(self && (message==WM_SYSCOLORCHANGE || message==WM_THEMECHANGED)) {
    InvalidateRect(hwnd,nullptr,TRUE); return DefWindowProcW(hwnd,message,wParam,lParam);
  }
  if(self && message==kProfileActiveMessage) {
    std::unique_ptr<ActivePayload> payload(reinterpret_cast<ActivePayload*>(lParam));
    { std::scoped_lock lock(self->profileMutex_); self->activeProfile_=std::move(payload->value); }
    self->pendingDraftId_=0;
    self->refreshControl(); return 0;
  }
  if(self && message==kDraftResultMessage) {
    std::unique_ptr<DraftPayload> payload(reinterpret_cast<DraftPayload*>(lParam));
    if(payload->value.requestId!=self->pendingDraftId_) return 0;
    self->pendingDraftId_=0;
    if(!payload->value.error.empty()) SetWindowTextW(self->draftStatus_,payload->value.error.c_str());
    else if(editText(self->instructionsEdit_)!=self->pendingDraftInstructions_)
      SetWindowTextW(self->draftStatus_,L"Draft ready, but your newer edits were kept. Generate again to replace them.");
    else {
      SetWindowTextW(self->instructionsEdit_,payload->value.additionalInstructions.c_str());
      SetWindowTextW(self->draftStatus_,L"Draft ready. Review or edit it, then Save profile to apply.");
    }
    return 0;
  }
  if(self && message==WM_COMMAND) {
    if (LOWORD(wParam) == kActionList && HIWORD(wParam) == CBN_SELCHANGE) {
      self->finishLearning(); self->preview_.clear(); self->shortcutNotice_.clear(); self->refreshShortcut(); return 0;
    }
    if (LOWORD(wParam)==kProfileList && HIWORD(wParam)==CBN_SELCHANGE) { self->loadSelectedProfile(); return 0; }
    switch(LOWORD(wParam)) {
      case kOverviewTab: self->showPage(0); break;
      case kProfilesTab: self->showPage(1); break;
      case kSettingsTab: self->showPage(2); break;
      case kCreateProfile: self->createProfile(); break;
      case kGenerateDraft: self->requestProfileDraft(); break;
      case kSaveProfile: self->saveProfile(); break;
      case kCancelProfile: self->pendingDraftId_=0; self->loadSelectedProfile(); SetWindowTextW(self->draftStatus_,L"Edits cancelled."); break;
      case kModelButton: self->toggleModel(); break;
      case kDebugButton: self->toggleDebug(); break;
      case kExitButton: self->exit_(); break;
      case kLearnButton: self->beginLearning(); break;
      case kDefaultButton: self->useDefault(); break;
      case kApplyButton: self->applyShortcut(); break;
      case kCancelButton: self->finishLearning(); self->preview_.clear(); self->shortcutNotice_.clear(); self->refreshShortcut(); break;
    }
    return 0;
  }
  if(self && message==WM_TIMER && wParam==kLearnTimer) { self->pollControllerLearning(); return 0; }
  if(self && self->learning_ && (message==WM_KEYDOWN || message==WM_SYSKEYDOWN)) {
    self->captureKey(wParam); return 0;
  }
  if(self && message==WM_APP+42) { self->refreshControl(); return 0; }
  if(self && message==WM_CLOSE) { self->finishLearning(); self->pendingDraftId_=0; ShowWindow(hwnd,SW_HIDE); return 0; }
  return DefWindowProcW(hwnd,message,wParam,lParam);
}
} // namespace d4r0
