#include "d4r0/Settings.h"
#include <fstream>
#include <sstream>
#include <cwctype>
#include <cmath>
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
      else if (key == "detectorThreshold") s.detectorThreshold = std::stof(value);
      else if (key == "minimumOcrConfidence") s.minimumOcrConfidence = std::stof(value);
      else if (key == "detectorLongSide") s.detectorLongSide = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "maxOcrBatch") s.maxOcrBatch = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "maxTranslationBatch") s.maxTranslationBatch = static_cast<std::uint32_t>(std::stoul(value));
      else if (key == "maxHeightRatio") s.maxHeightRatio = std::stof(value);
      else if (key == "maxVerticalGapRatio") s.maxVerticalGapRatio = std::stof(value);
      else if (key == "maxGroupLines") s.maxGroupLines = static_cast<std::uint32_t>(std::stoul(value));
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
  auto temporary = path_; temporary += L".tmp";
  std::ofstream out(temporary, std::ios::trunc); if (!out) return false;
  out << "model=" << (s.selectedModel == ModelChoice::TranslateGemma12B ? "12b" : "4b") << '\n'
      << "stabilityDelayMs=" << s.stabilityDelayMs << '\n' << "ocrCadenceMs=" << s.ocrCadenceMs << '\n'
      << "captureMonitorIndex=" << s.captureMonitorIndex << '\n'
      << "detectorThreshold=" << s.detectorThreshold << '\n'
      << "minimumOcrConfidence=" << s.minimumOcrConfidence << '\n'
      << "detectorLongSide=" << s.detectorLongSide << '\n' << "maxOcrBatch=" << s.maxOcrBatch << '\n'
      << "maxTranslationBatch=" << s.maxTranslationBatch << '\n'
      << "maxHeightRatio=" << s.maxHeightRatio << '\n'
      << "maxVerticalGapRatio=" << s.maxVerticalGapRatio << '\n'
      << "maxGroupLines=" << s.maxGroupLines << '\n'
      << "lowConfidenceOpacity=" << s.lowConfidenceOpacity << '\n'
      << "showDiagnostics=" << s.showDiagnostics << '\n' << "replayEnabled=" << s.replayEnabled << '\n'
      << "toggleShortcut=" << asciiShortcut(s.toggleShortcut) << '\n'
      << "originalShortcut=" << asciiShortcut(s.originalShortcut) << '\n'
      << "diagnosticsShortcut=" << asciiShortcut(s.diagnosticsShortcut) << '\n'
      << "llamaExecutable=" << s.llamaExecutable.string() << '\n' << "model4b=" << s.model4b.string() << '\n' << "model12b=" << s.model12b.string() << '\n'
      << "ocrDetector=" << s.ocrDetector.string() << '\n' << "ocrRecognizer=" << s.ocrRecognizer.string() << '\n'
      << "ocrDictionary=" << s.ocrDictionary.string() << '\n' << "ocrAdapter=" << s.ocrAdapter << '\n';
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
  if (s.maxOcrBatch == 0 || s.maxOcrBatch > 32) return "maxOcrBatch must be 1..32";
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
  if (!std::filesystem::is_regular_file(s.llamaExecutable)) return "llamaExecutable does not exist";
  if (!std::filesystem::is_regular_file(s.model4b)) return "model4b does not exist";
  if (s.selectedModel == ModelChoice::TranslateGemma12B && !std::filesystem::is_regular_file(s.model12b))
    return "model12b does not exist";
  if (!std::filesystem::is_regular_file(s.ocrDetector)) return "ocrDetector does not exist";
  if (!std::filesystem::is_regular_file(s.ocrRecognizer)) return "ocrRecognizer does not exist";
  if (!std::filesystem::is_regular_file(s.ocrDictionary)) return "ocrDictionary does not exist";
  if (!parseShortcut(s.toggleShortcut) || !parseShortcut(s.originalShortcut) ||
      !parseShortcut(s.diagnosticsShortcut)) return "shortcut syntax is invalid";
  if (s.toggleShortcut == s.originalShortcut || s.toggleShortcut == s.diagnosticsShortcut ||
      s.originalShortcut == s.diagnosticsShortcut) return "shortcuts must be distinct";
  return std::nullopt;
}

SettingsApply classifySettingsChanges(const PipelineSettings& a, const PipelineSettings& b) {
  SettingsApply result = SettingsApply::None;
  if (a.lowConfidenceOpacity != b.lowConfidenceOpacity || a.showDiagnostics != b.showDiagnostics)
    result = result | SettingsApply::LiveDisplay;
  if (a.stabilityDelayMs != b.stabilityDelayMs || a.ocrCadenceMs != b.ocrCadenceMs ||
      a.detectorLongSide != b.detectorLongSide || a.maxOcrBatch != b.maxOcrBatch ||
      a.maxTranslationBatch != b.maxTranslationBatch || a.detectorThreshold != b.detectorThreshold ||
      a.minimumOcrConfidence != b.minimumOcrConfidence || a.maxHeightRatio != b.maxHeightRatio ||
      a.maxVerticalGapRatio != b.maxVerticalGapRatio || a.maxGroupLines != b.maxGroupLines)
    result = result | SettingsApply::Worker;
  if (a.selectedModel != b.selectedModel || a.llamaExecutable != b.llamaExecutable ||
      a.model4b != b.model4b || a.model12b != b.model12b || a.ocrDetector != b.ocrDetector ||
      a.ocrRecognizer != b.ocrRecognizer || a.ocrDictionary != b.ocrDictionary || a.ocrAdapter != b.ocrAdapter)
    result = result | SettingsApply::Pipeline;
  if (a.captureMonitorIndex != b.captureMonitorIndex) result = result | SettingsApply::Capture;
  if (a.replayEnabled != b.replayEnabled) result = result | SettingsApply::Replay;
  if (a.toggleShortcut != b.toggleShortcut || a.originalShortcut != b.originalShortcut ||
      a.diagnosticsShortcut != b.diagnosticsShortcut)
    result = result | SettingsApply::Shortcuts;
  return result;
}
} // namespace d4r0
