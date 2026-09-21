#include "d4r0/Settings.h"
#include <fstream>
#include <sstream>
#include <cwctype>
namespace d4r0 {
namespace {
std::string asciiShortcut(const std::wstring& value) {
  std::string result; result.reserve(value.size());
  for (const auto character : value) result.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
  return result;
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
PipelineSettings SettingsStore::load() const {
  PipelineSettings s; std::ifstream in(path_); std::string key, value;
  while (std::getline(in, key, '=') && std::getline(in, value)) {
    try {
      if (key == "model") s.selectedModel = value == "12b" ? ModelChoice::TranslateGemma12B : ModelChoice::TranslateGemma4B;
      else if (key == "stabilityDelayMs") s.stabilityDelayMs = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "ocrCadenceMs") s.ocrCadenceMs = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "captureMonitorIndex") s.captureMonitorIndex = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "minimumOcrConfidence") s.minimumOcrConfidence = std::stof(value);
      else if (key == "detectorLongSide") s.detectorLongSide = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "maxOcrBatch") s.maxOcrBatch = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "maxTranslationBatch") s.maxTranslationBatch = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "lowConfidenceOpacity") s.lowConfidenceOpacity = std::stof(value);
      else if (key == "showDiagnostics") s.showDiagnostics = value == "1";
      else if (key == "replayEnabled") s.replayEnabled = value == "1";
      else if (key == "toggleShortcut") s.toggleShortcut.assign(value.begin(),value.end());
      else if (key == "originalShortcut") s.originalShortcut.assign(value.begin(),value.end());
      else if (key == "diagnosticsShortcut") s.diagnosticsShortcut.assign(value.begin(),value.end());
      else if (key == "llamaExecutable") s.llamaExecutable = std::filesystem::path(value);
      else if (key == "model4b") s.model4b = std::filesystem::path(value);
      else if (key == "model12b") s.model12b = std::filesystem::path(value);
      else if (key == "ocrDetector") s.ocrDetector = std::filesystem::path(value);
      else if (key == "ocrRecognizer") s.ocrRecognizer = std::filesystem::path(value);
      else if (key == "ocrDictionary") s.ocrDictionary = std::filesystem::path(value);
      else if (key == "ocrAdapter") s.ocrAdapter = std::stoi(value);
    } catch (const std::exception&) { }
  } return s;
}
bool SettingsStore::save(const PipelineSettings& s) const {
  std::error_code error; std::filesystem::create_directories(path_.parent_path(), error); if (error) return false;
  std::ofstream out(path_); if (!out) return false;
  out << "model=" << (s.selectedModel == ModelChoice::TranslateGemma12B ? "12b" : "4b") << '\n'
      << "stabilityDelayMs=" << s.stabilityDelayMs << '\n' << "ocrCadenceMs=" << s.ocrCadenceMs << '\n'
      << "captureMonitorIndex=" << s.captureMonitorIndex << '\n'
      << "minimumOcrConfidence=" << s.minimumOcrConfidence << '\n'
      << "detectorLongSide=" << s.detectorLongSide << '\n' << "maxOcrBatch=" << s.maxOcrBatch << '\n'
      << "maxTranslationBatch=" << s.maxTranslationBatch << '\n'
      << "lowConfidenceOpacity=" << s.lowConfidenceOpacity << '\n'
      << "showDiagnostics=" << s.showDiagnostics << '\n' << "replayEnabled=" << s.replayEnabled << '\n'
      << "toggleShortcut=" << asciiShortcut(s.toggleShortcut) << '\n'
      << "originalShortcut=" << asciiShortcut(s.originalShortcut) << '\n'
      << "diagnosticsShortcut=" << asciiShortcut(s.diagnosticsShortcut) << '\n'
      << "llamaExecutable=" << s.llamaExecutable.string() << '\n' << "model4b=" << s.model4b.string() << '\n' << "model12b=" << s.model12b.string() << '\n'
      << "ocrDetector=" << s.ocrDetector.string() << '\n' << "ocrRecognizer=" << s.ocrRecognizer.string() << '\n'
      << "ocrDictionary=" << s.ocrDictionary.string() << '\n' << "ocrAdapter=" << s.ocrAdapter << '\n';
  return static_cast<bool>(out);
}
} // namespace d4r0
