#include "d4r0/Settings.h"
#include <fstream>
#include <sstream>
#include <cwctype>
#include <cmath>
#include <algorithm>
#include <codecvt>
#include <locale>
#ifdef _WIN32
#include <windows.h>
#endif
namespace d4r0 {
namespace {
std::string asciiShortcut(const std::wstring& value) {
  std::string result; result.reserve(value.size());
  for (const auto character : value) result.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
  return result;
}
std::string utf8(const std::wstring& value) {
  try {
#ifdef _WIN32
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
#else
    std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
#endif
    return convert.to_bytes(value);
  } catch (const std::range_error&) { return {}; }
}
std::wstring fromUtf8(const std::string& value) {
  try {
#ifdef _WIN32
    std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>> convert;
#else
    std::wstring_convert<std::codecvt_utf8<wchar_t>> convert;
#endif
    return convert.from_bytes(value);
  } catch (const std::range_error&) { return {}; }
}
std::string escape(const std::string& value) {
  constexpr char hex[] = "0123456789ABCDEF";
  std::string result;
  for (const auto c : value) {
    const auto byte = static_cast<unsigned char>(c);
    if ((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
        (byte >= '0' && byte <= '9') || byte == '-' || byte == '_' || byte == '.') result.push_back(c);
    else { result.push_back('%'); result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
  }
  return result;
}
std::string unescape(const std::string& value) {
  auto hexValue = [](char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };
  std::string result;
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] != '%' || i + 2 >= value.size()) { result.push_back(value[i]); continue; }
    const int high = hexValue(value[i + 1]), low = hexValue(value[i + 2]);
    if (high < 0 || low < 0) { result.push_back(value[i]); continue; }
    result.push_back(static_cast<char>((high << 4) | low)); i += 2;
  }
  return result;
}
std::string encoded(const std::wstring& value) { return escape(utf8(value)); }
std::wstring decoded(const std::string& value) { return fromUtf8(unescape(value)); }
std::string profileKey(std::size_t index, const char* field) {
  return "profile." + std::to_string(index) + "." + field;
}
}
std::optional<ShortcutBinding> parseShortcut(std::wstring_view shortcut) {
  ShortcutBinding binding;
  bool keySeen = false;
  for (std::size_t start = 0; start <= shortcut.size();) {
    const auto end = shortcut.find(L'+',start);
    auto token = shortcut.substr(start,end == std::wstring_view::npos ? shortcut.size()-start : end-start);
    while (!token.empty() && std::iswspace(token.front())) token.remove_prefix(1);
    while (!token.empty() && std::iswspace(token.back())) token.remove_suffix(1);
    std::wstring lower(token);
    for (auto& value : lower) value = wchar_t(std::towlower(value));
    std::uint32_t modifier{};
    if (lower == L"ctrl" || lower == L"control") modifier = ShortcutCtrl;
    else if (lower == L"shift") modifier = ShortcutShift;
    else if (lower == L"alt") modifier = ShortcutAlt;
    if (modifier) {
      if (binding.modifiers & modifier || keySeen) return std::nullopt;
      binding.modifiers |= modifier;
    } else {
      if (keySeen || lower.empty()) return std::nullopt;
      if (lower == L"tab") binding.key = 0x09;
      else if (lower == L"space") binding.key = 0x20;
      else if (lower == L"escape" || lower == L"esc") binding.key = 0x1B;
      else if (lower.size() == 1 && ((lower[0] >= L'a' && lower[0] <= L'z') ||
                                    (lower[0] >= L'0' && lower[0] <= L'9')))
        binding.key = std::uint32_t(std::towupper(lower[0]));
      else if (lower.starts_with(L'f')) {
        try {
          std::size_t consumed{}; const auto number = std::stoul(lower.substr(1),&consumed);
          if (consumed == lower.size()-1 && number >= 1 && number <= 24) binding.key = 0x70+number-1;
        }
        catch (const std::exception&) {}
      }
      if (!binding.key) return std::nullopt;
      keySeen = true;
    }
    if (end == std::wstring_view::npos) break;
    start = end+1;
  }
  if (!keySeen || !binding.modifiers) return std::nullopt;
  return binding;
}
std::optional<ControllerBinding> parseControllerBinding(std::wstring_view value) {
  std::wstring lower(value);
  for (auto& character : lower) character = wchar_t(std::towlower(character));
  if (lower == L"none") return ControllerBinding{};
  ControllerBinding result;
  result.raw = lower.starts_with(L"raw:");
  result.hid = lower.starts_with(L"hid:");
  if (result.raw || result.hid) lower.erase(0, 4);
  constexpr std::pair<std::wstring_view, std::uint32_t> buttons[] = {
    {L"menu", 0x1}, {L"view", 0x2}, {L"a", 0x4}, {L"b", 0x8},
    {L"x", 0x10}, {L"y", 0x20}, {L"dpadup", 0x40}, {L"dpaddown", 0x80},
    {L"dpadleft", 0x100}, {L"dpadright", 0x200},
    {L"leftshoulder", 0x400}, {L"rightshoulder", 0x800},
    {L"leftthumbstick", 0x1000}, {L"rightthumbstick", 0x2000},
    {L"paddle1", 0x4000}, {L"paddle2", 0x8000},
    {L"paddle3", 0x10000}, {L"paddle4", 0x20000},
    {L"lefttrigger", 0x40000}, {L"righttrigger", 0x80000}
  };
  for (std::size_t start = 0; start < lower.size();) {
    const auto end = lower.find(L'+', start);
    const auto token = std::wstring_view(lower).substr(start, end == std::wstring::npos ? end : end-start);
    if (token.empty()) return std::nullopt;
    if (result.raw || result.hid) {
      if (token.size() > (result.hid ? 5 : 3) ||
          token.find_first_not_of(L"0123456789") != std::wstring_view::npos)
        return std::nullopt;
      const auto index = std::stoul(std::wstring(token));
      if (index > (result.hid ? 65535u : 255u) ||
          std::find(result.rawButtons.begin(), result.rawButtons.end(), index) != result.rawButtons.end())
        return std::nullopt;
      result.rawButtons.push_back(static_cast<std::uint32_t>(index));
    } else {
      bool found = false;
      for (const auto& [name, button] : buttons) if (token == name && !(result.buttons & button)) {
        result.buttons |= button; found = true; break;
      }
      if (!found) return std::nullopt;
    }
    if (end == std::wstring::npos) break;
    start = end + 1;
    if (start == lower.size()) return std::nullopt;
  }
  if ((result.raw || result.hid) ? result.rawButtons.empty() : !result.buttons) return std::nullopt;
  return result;
}
std::wstring controllerBindingName(const ControllerBinding& binding) {
  if (binding.raw || binding.hid) {
    std::wstring name = binding.hid ? L"Hid:" : L"Raw:";
    for (const auto index : binding.rawButtons) {
      if (name.size() > 4) name += L"+";
      name += std::to_wstring(index);
    }
    return name;
  }
  if (!binding.buttons) return L"None";
  constexpr std::pair<std::wstring_view, std::uint32_t> names[] = {
    {L"Menu", 0x1}, {L"View", 0x2}, {L"A", 0x4}, {L"B", 0x8},
    {L"X", 0x10}, {L"Y", 0x20}, {L"DPadUp", 0x40}, {L"DPadDown", 0x80},
    {L"DPadLeft", 0x100}, {L"DPadRight", 0x200},
    {L"LeftShoulder", 0x400}, {L"RightShoulder", 0x800},
    {L"LeftThumbstick", 0x1000}, {L"RightThumbstick", 0x2000},
    {L"Paddle1", 0x4000}, {L"Paddle2", 0x8000},
    {L"Paddle3", 0x10000}, {L"Paddle4", 0x20000},
    {L"LeftTrigger", 0x40000}, {L"RightTrigger", 0x80000}
  };
  std::wstring name;
  for (const auto& [label, button] : names) if (binding.buttons & button) {
    if (!name.empty()) name += L"+";
    name += label;
  }
  return name;
}
PipelineSettings SettingsStore::load() const {
  PipelineSettings s; std::ifstream in(path_); std::string key, value;
  while (std::getline(in, key, '=') && std::getline(in, value)) {
    try {
      if (key == "profiles.count") s.profiles.clear(), s.profiles.resize(std::min<std::size_t>(std::stoul(value), 256));
      else if (key.starts_with("profile.")) {
        const auto dot = key.find('.', 8);
        if (dot == std::string::npos) continue;
        const auto index = static_cast<std::size_t>(std::stoul(key.substr(8, dot - 8)));
        if (index >= s.profiles.size()) continue;
        auto& profile = s.profiles[index];
        const auto field = key.substr(dot + 1);
        if (field == "id") profile.id = unescape(value);
        else if (field == "name") profile.displayName = decoded(value);
        else if (field == "path") profile.executablePath = std::filesystem::path(decoded(value));
        else if (field == "template") {
          const auto kind = std::stoul(value);
          if (kind <= static_cast<unsigned long>(ProfileTemplate::General)) profile.templateKind = static_cast<ProfileTemplate>(kind);
        } else if (field == "source") profile.sourceLanguage = unescape(value);
        else if (field == "target") profile.targetLanguage = unescape(value);
        else if (field == "instructions") profile.additionalInstructions = decoded(value);
      }
      else if (key == "model") s.selectedModel = value == "12b" ? ModelChoice::TranslateGemma12B : ModelChoice::TranslateGemma4B;
      else if (key == "stabilityDelayMs") s.stabilityDelayMs = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "ocrCadenceMs") s.ocrCadenceMs = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "captureMonitorIndex") s.captureMonitorIndex = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "detectorThreshold") s.detectorThreshold = std::stof(value);
      else if (key == "minimumOcrConfidence") s.minimumOcrConfidence = std::stof(value);
      else if (key == "detectorLongSide") s.detectorLongSide = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "maxTranslationBatch") s.maxTranslationBatch = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "maxHeightRatio") s.maxHeightRatio = std::stof(value);
      else if (key == "maxVerticalGapRatio") s.maxVerticalGapRatio = std::stof(value);
      else if (key == "maxGroupLines") s.maxGroupLines = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "lowConfidenceOpacity") s.lowConfidenceOpacity = std::stof(value);
      // Debug capture requires a fresh, explicit toggle on every launch.
      else if (key == "replayEnabled") s.replayEnabled = value == "1";
      else if (key == "toggleShortcut") s.toggleShortcut.assign(value.begin(),value.end());
      else if (key == "originalShortcut") s.originalShortcut.assign(value.begin(),value.end());
      else if (key == "diagnosticsShortcut") s.diagnosticsShortcut.assign(value.begin(),value.end());
      else if (key == "exitShortcut") s.exitShortcut.assign(value.begin(),value.end());
      else if (key == "toggleControllerButton") s.toggleControllerButton.assign(value.begin(),value.end());
      else if (key == "llamaExecutable") s.llamaExecutable = std::filesystem::path(value);
      else if (key == "model4b") s.model4b = std::filesystem::path(value);
      else if (key == "model12b") s.model12b = std::filesystem::path(value);
      else if (key == "ocrDetector") s.ocrDetector = std::filesystem::path(value);
      else if (key == "ocrRecognizer") s.ocrRecognizer = std::filesystem::path(value);
      else if (key == "ocrDictionary") s.ocrDictionary = std::filesystem::path(value);
      else if (key == "ocrAdapter") s.ocrAdapter = std::stoi(value);
    } catch (const std::exception&) { }
  }
  const auto loadedProfiles = std::move(s.profiles);
  s.profiles = defaultProfiles();
  for (auto& fallback : s.profiles) {
    const auto found = std::find_if(loadedProfiles.begin(), loadedProfiles.end(), [&](const auto& profile) {
      return profile.id == fallback.id;
    });
    if (found != loadedProfiles.end() && found->executablePath.empty() &&
        found->templateKind == fallback.templateKind && !validateProfile(*found)) fallback = *found;
  }
  for (const auto& profile : loadedProfiles) {
    if (profile.executablePath.empty() || validateProfile(profile)) continue;
    const auto duplicate = std::any_of(s.profiles.begin(), s.profiles.end(), [&](const auto& existing) {
      return existing.id == profile.id || sameExecutablePath(existing.executablePath, profile.executablePath);
    });
    if (!duplicate) s.profiles.push_back(profile);
  }
  return s;
}
bool SettingsStore::save(const PipelineSettings& s) const {
  std::error_code error; std::filesystem::create_directories(path_.parent_path(), error); if (error) return false;
  auto temporary = path_; temporary += L".tmp";
  std::ofstream out(temporary, std::ios::trunc); if (!out) return false;
  out << "model=" << (s.selectedModel == ModelChoice::TranslateGemma12B ? "12b" : "4b") << '\n'
      << "stabilityDelayMs=" << s.stabilityDelayMs << '\n' << "ocrCadenceMs=" << s.ocrCadenceMs << '\n'
      << "captureMonitorIndex=" << s.captureMonitorIndex << '\n'
      << "detectorThreshold=" << s.detectorThreshold << '\n'
      << "minimumOcrConfidence=" << s.minimumOcrConfidence << '\n'
      << "detectorLongSide=" << s.detectorLongSide << '\n'
      << "maxTranslationBatch=" << s.maxTranslationBatch << '\n'
      << "maxHeightRatio=" << s.maxHeightRatio << '\n'
      << "maxVerticalGapRatio=" << s.maxVerticalGapRatio << '\n'
      << "maxGroupLines=" << s.maxGroupLines << '\n'
      << "lowConfidenceOpacity=" << s.lowConfidenceOpacity << '\n'
      << "showDiagnostics=" << s.showDiagnostics << '\n' << "replayEnabled=" << s.replayEnabled << '\n'
      << "toggleShortcut=" << asciiShortcut(s.toggleShortcut) << '\n'
      << "originalShortcut=" << asciiShortcut(s.originalShortcut) << '\n'
      << "diagnosticsShortcut=" << asciiShortcut(s.diagnosticsShortcut) << '\n'
      << "exitShortcut=" << asciiShortcut(s.exitShortcut) << '\n'
      << "toggleControllerButton=" << asciiShortcut(s.toggleControllerButton) << '\n'
      << "llamaExecutable=" << s.llamaExecutable.string() << '\n' << "model4b=" << s.model4b.string() << '\n' << "model12b=" << s.model12b.string() << '\n'
      << "ocrDetector=" << s.ocrDetector.string() << '\n' << "ocrRecognizer=" << s.ocrRecognizer.string() << '\n'
      << "ocrDictionary=" << s.ocrDictionary.string() << '\n' << "ocrAdapter=" << s.ocrAdapter << '\n';
  out << "profiles.count=" << s.profiles.size() << '\n';
  for (std::size_t i = 0; i < s.profiles.size(); ++i) {
    const auto& profile = s.profiles[i];
    out << profileKey(i, "id") << '=' << escape(profile.id) << '\n'
        << profileKey(i, "name") << '=' << encoded(profile.displayName) << '\n'
        << profileKey(i, "path") << '=' << encoded(profile.executablePath.wstring()) << '\n'
        << profileKey(i, "template") << '=' << static_cast<unsigned>(profile.templateKind) << '\n'
        << profileKey(i, "source") << '=' << escape(profile.sourceLanguage) << '\n'
        << profileKey(i, "target") << '=' << escape(profile.targetLanguage) << '\n'
        << profileKey(i, "instructions") << '=' << encoded(profile.additionalInstructions) << '\n';
  }
  out.flush();
  if (!out) { out.close(); std::filesystem::remove(temporary, error); return false; }
  out.close();
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), path_.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    std::filesystem::remove(temporary, error);
    return false;
  }
#else
  std::filesystem::rename(temporary, path_, error);
  if (error) { std::filesystem::remove(temporary, error); return false; }
#endif
  return true;
}
std::optional<std::string> validateSettings(const PipelineSettings& s) {
  if (s.stabilityDelayMs == 0 || s.stabilityDelayMs > 2000) return "stabilityDelayMs must be 1..2000";
  if (s.ocrCadenceMs < 20 || s.ocrCadenceMs > 2000) return "ocrCadenceMs must be 20..2000";
  if (s.detectorLongSide < 32 || s.detectorLongSide > 2048) return "detectorLongSide must be 32..2048";
  if (s.maxTranslationBatch == 0 || s.maxTranslationBatch > 16) return "maxTranslationBatch must be 1..16";
  if (!std::isfinite(s.detectorThreshold) || s.detectorThreshold <= 0 || s.detectorThreshold >= 1)
    return "detectorThreshold must be between 0 and 1";
  if (!std::isfinite(s.minimumOcrConfidence) || s.minimumOcrConfidence < 0 || s.minimumOcrConfidence > 1)
    return "minimumOcrConfidence must be between 0 and 1";
  if (!std::isfinite(s.lowConfidenceOpacity) || s.lowConfidenceOpacity < 0 || s.lowConfidenceOpacity > 1)
    return "lowConfidenceOpacity must be between 0 and 1";
  if (!std::isfinite(s.maxHeightRatio) || s.maxHeightRatio < 1 || s.maxHeightRatio > 3)
    return "maxHeightRatio must be 1..3";
  if (!std::isfinite(s.maxVerticalGapRatio) || s.maxVerticalGapRatio <= 0 || s.maxVerticalGapRatio > 3)
    return "maxVerticalGapRatio must be greater than 0 and at most 3";
  if (s.maxGroupLines == 0 || s.maxGroupLines > 32) return "maxGroupLines must be 1..32";
  if (s.ocrAdapter < -1) return "ocrAdapter must be -1 or a nonnegative adapter index";
  if (s.selectedModel != ModelChoice::TranslateGemma4B && s.selectedModel != ModelChoice::TranslateGemma12B)
    return "selectedModel is invalid";
  for (const auto& profile : s.profiles) if (auto error = validateProfile(profile)) return error;
  if (!std::filesystem::is_regular_file(s.llamaExecutable)) return "llamaExecutable does not exist";
  if (!std::filesystem::is_regular_file(s.model4b)) return "model4b does not exist";
  if (s.selectedModel == ModelChoice::TranslateGemma12B && !std::filesystem::is_regular_file(s.model12b))
    return "model12b does not exist";
  if (!std::filesystem::is_regular_file(s.ocrDetector)) return "ocrDetector does not exist";
  if (!std::filesystem::is_regular_file(s.ocrRecognizer)) return "ocrRecognizer does not exist";
  if (!std::filesystem::is_regular_file(s.ocrDictionary)) return "ocrDictionary does not exist";
  return validateShortcuts(s);
}
std::optional<std::string> validateShortcuts(const PipelineSettings& s) {
  if (!parseShortcut(s.toggleShortcut) || !parseShortcut(s.originalShortcut) ||
      !parseShortcut(s.diagnosticsShortcut) || !parseShortcut(s.exitShortcut)) return "shortcut syntax is invalid";
  if (!parseControllerBinding(s.toggleControllerButton)) return "toggleControllerButton is invalid";
  if (s.toggleShortcut == s.originalShortcut || s.toggleShortcut == s.diagnosticsShortcut ||
      s.originalShortcut == s.diagnosticsShortcut || s.toggleShortcut == s.exitShortcut ||
      s.originalShortcut == s.exitShortcut || s.diagnosticsShortcut == s.exitShortcut)
    return "shortcuts must be distinct";
  return std::nullopt;
}

} // namespace d4r0
