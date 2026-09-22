#include "d4r0/TrayController.h"
#include "d4r0/DebugLog.h"
#include <shellapi.h>

namespace d4r0 {
namespace {
constexpr UINT kTrayMessage = WM_APP + 41;
constexpr UINT kOpen = 1, kModel = 2, kDebug = 3, kExit = 4;
constexpr int kModelButton = 101, kDebugButton = 102, kExitButton = 103;
const wchar_t* kMessageClass = L"d4r0.TrayMessage";
const wchar_t* kControlClass = L"d4r0.ControlWindow";
UINT taskbarCreated() { static const UINT value=RegisterWindowMessageW(L"TaskbarCreated"); return value; }
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
  icon_.cbSize=sizeof(icon_); icon_.hWnd=messageWindow_; icon_.uID=1; icon_.uFlags=NIF_MESSAGE|NIF_ICON|NIF_TIP; icon_.uCallbackMessage=kTrayMessage; icon_.hIcon=LoadIcon(nullptr,IDI_APPLICATION); lstrcpyW(icon_.szTip,L"d4r0 local translator");
  created_=Shell_NotifyIconW(NIM_ADD,&icon_) != FALSE;
  if(created_) Shell_NotifyIconW(NIM_SETVERSION,&icon_);
  return created_;
}
void TrayController::destroy() {
  if(controlWindow_) { DestroyWindow(controlWindow_); controlWindow_=nullptr; }
  if(created_) { Shell_NotifyIconW(NIM_DELETE,&icon_); created_=false; }
  if(messageWindow_) { DestroyWindow(messageWindow_); messageWindow_=nullptr; }
}
void TrayController::setStatus(std::wstring status) { { std::scoped_lock lock(statusMutex_); status_=std::move(status); } if(controlWindow_) PostMessageW(controlWindow_,WM_APP+42,0,0); }
void TrayController::showMenu() {
  HMENU menu=CreatePopupMenu(); AppendMenuW(menu,MF_STRING,kOpen,L"Open controls"); AppendMenuW(menu,MF_STRING,kModel,settings_.selectedModel==ModelChoice::TranslateGemma4B?L"Model: 4B (restart applies)":L"Model: 12B (restart applies)"); AppendMenuW(menu,MF_STRING| (settings_.showDiagnostics?MF_CHECKED:0),kDebug,L"Show debug overlay"); AppendMenuW(menu,MF_SEPARATOR,0,nullptr); AppendMenuW(menu,MF_STRING,kExit,L"Exit d4r0");
  POINT point{}; GetCursorPos(&point); SetForegroundWindow(messageWindow_); const auto command=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,point.x,point.y,0,messageWindow_,nullptr); DestroyMenu(menu);
  if(command==kOpen) showControl(); else if(command==kModel) { settings_.selectedModel=settings_.selectedModel==ModelChoice::TranslateGemma4B?ModelChoice::TranslateGemma12B:ModelChoice::TranslateGemma4B; store_.save(settings_); showControl(); } else if(command==kDebug) { settings_.showDiagnostics=!settings_.showDiagnostics; overlay_.setDiagnostics(settings_.showDiagnostics); store_.save(settings_); showControl(); } else if(command==kExit) exit_();
}
void TrayController::showControl() {
  if(!controlWindow_) {
    controlWindow_=CreateWindowExW(0,kControlClass,L"d4r0 controls",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,560,240,nullptr,nullptr,instance_,this);
    if(!controlWindow_) return;
    CreateWindowW(L"STATIC",L"d4r0 local translation",WS_CHILD|WS_VISIBLE,16,14,500,24,controlWindow_,nullptr,instance_,nullptr);
    CreateWindowW(L"BUTTON",L"Switch model (restart required)",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,16,48,240,30,controlWindow_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kModelButton)),instance_,nullptr);
    CreateWindowW(L"BUTTON",L"Toggle debug overlay",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,270,48,180,30,controlWindow_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kDebugButton)),instance_,nullptr);
    CreateWindowW(L"BUTTON",L"Exit",WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,454,48,70,30,controlWindow_,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kExitButton)),instance_,nullptr);
    statusLabel_=CreateWindowW(L"STATIC",L"",WS_CHILD|WS_VISIBLE|SS_LEFT,16,94,508,110,controlWindow_,nullptr,instance_,nullptr);
  }
  refreshControl(); ShowWindow(controlWindow_,SW_SHOWNOACTIVATE); SetForegroundWindow(controlWindow_);
}
void TrayController::refreshControl() {
  if(!statusLabel_) return;
  std::wstring status; { std::scoped_lock lock(statusMutex_); status=status_; }
  std::wstring text=L"Model: "; text += settings_.selectedModel==ModelChoice::TranslateGemma4B?L"TranslateGemma 4B":L"TranslateGemma 12B"; text += settings_.showDiagnostics?L"\r\nDebug overlay: enabled\r\n":L"\r\nDebug overlay: disabled\r\n"; text += L"\r\n"+status; SetWindowTextW(statusLabel_,text.c_str());
}
LRESULT CALLBACK TrayController::messageProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
  auto* self=reinterpret_cast<TrayController*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if(message==WM_NCCREATE) { self=static_cast<TrayController*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
  if(self && message==kTrayMessage && (lParam==WM_LBUTTONUP || lParam==WM_RBUTTONUP || lParam==WM_CONTEXTMENU)) { self->showMenu(); return 0; }
  if(self && message==taskbarCreated()) { Shell_NotifyIconW(NIM_ADD,&self->icon_); return 0; }
  return DefWindowProcW(hwnd,message,wParam,lParam);
}
LRESULT CALLBACK TrayController::controlProc(HWND hwnd,UINT message,WPARAM wParam,LPARAM lParam) {
  auto* self=reinterpret_cast<TrayController*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
  if(message==WM_NCCREATE) { self=static_cast<TrayController*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams); SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self)); }
  if(self && message==WM_COMMAND) { switch(LOWORD(wParam)) { case kModelButton: self->settings_.selectedModel=self->settings_.selectedModel==ModelChoice::TranslateGemma4B?ModelChoice::TranslateGemma12B:ModelChoice::TranslateGemma4B; self->store_.save(self->settings_); self->refreshControl(); break; case kDebugButton: self->settings_.showDiagnostics=!self->settings_.showDiagnostics; self->overlay_.setDiagnostics(self->settings_.showDiagnostics); self->store_.save(self->settings_); self->refreshControl(); break; case kExitButton: self->exit_(); break; } return 0; }
  if(self && message==WM_APP+42) { self->refreshControl(); return 0; }
  if(self && message==WM_CLOSE) { ShowWindow(hwnd,SW_HIDE); return 0; }
  return DefWindowProcW(hwnd,message,wParam,lParam);
}
} // namespace d4r0
