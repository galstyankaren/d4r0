#include "d4r0/AppDetection.h"
#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace d4r0 {
namespace {
struct MonitorSearch {
  std::uint32_t wanted{};
  std::uint32_t current{};
  HMONITOR found{};
};

BOOL CALLBACK findMonitor(HMONITOR monitor, HDC, LPRECT, LPARAM context) {
  auto& search = *reinterpret_cast<MonitorSearch*>(context);
  if (search.current++ != search.wanted) return TRUE;
  search.found = monitor;
  return FALSE;
}

std::wstring lower(std::wstring value) {
  for (auto& character : value) character = static_cast<wchar_t>(std::towlower(character));
  return value;
}

} // namespace

ProfileTemplate guessAppTemplate(const std::filesystem::path& executablePath) {
  const auto name = lower(executablePath.filename().wstring());
  constexpr std::array<std::wstring_view, 7> browsers{
      L"chrome.exe", L"msedge.exe", L"firefox.exe", L"brave.exe", L"opera.exe",
      L"vivaldi.exe", L"iexplore.exe"};
  if (std::find(browsers.begin(), browsers.end(), name) != browsers.end())
    return ProfileTemplate::Browser;

  auto path = lower(executablePath.wstring());
  std::replace(path.begin(), path.end(), L'/', L'\\');
  for (const auto marker : {L"\\steamapps\\common\\", L"\\epic games\\",
                            L"\\gog galaxy\\games\\", L"\\xboxgames\\"})
    if (path.find(marker) != std::wstring::npos) return ProfileTemplate::Game;

  constexpr std::array<std::wstring_view, 14> professional{
      L"winword.exe", L"excel.exe", L"powerpnt.exe", L"outlook.exe",
      L"photoshop.exe", L"illustrator.exe", L"indesign.exe", L"acrobat.exe",
      L"blender.exe", L"acad.exe", L"code.exe", L"devenv.exe",
      L"notepad++.exe", L"sldworks.exe"};
  if (std::find(professional.begin(), professional.end(), name) != professional.end())
    return ProfileTemplate::Professional;
  return ProfileTemplate::General;
}

AppDetector::AppDetector(std::uint32_t monitorIndex) {
  MonitorSearch search{.wanted = monitorIndex};
  EnumDisplayMonitors(nullptr, nullptr, findMonitor, reinterpret_cast<LPARAM>(&search));
  monitor_ = search.found;
}

std::optional<AppIdentity> AppDetector::focusedApp() const {
  if (!monitor_) return std::nullopt;
  const auto window = GetForegroundWindow();
  if (!window || IsIconic(window) || MonitorFromWindow(window, MONITOR_DEFAULTTONULL) != monitor_)
    return std::nullopt;
  DWORD processId{};
  if (!GetWindowThreadProcessId(window, &processId) || !processId || processId == GetCurrentProcessId())
    return std::nullopt;
  const auto process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
  if (!process) return AppIdentity{{}, L"Unknown application", ProfileTemplate::General};
  std::wstring image(32768, L'\0');
  DWORD length = static_cast<DWORD>(image.size());
  const bool found = QueryFullProcessImageNameW(process, 0, image.data(), &length) != FALSE;
  CloseHandle(process);
  if (!found || !length) return AppIdentity{{}, L"Unknown application", ProfileTemplate::General};
  image.resize(length);
  const std::filesystem::path path(image);
  return AppIdentity{path, path.stem().wstring(), guessAppTemplate(path)};
}
} // namespace d4r0
