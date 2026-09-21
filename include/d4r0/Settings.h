#pragma once
#include "d4r0/Types.h"
#include <filesystem>
#include <optional>
#include <string_view>

namespace d4r0 {
enum ShortcutModifier : std::uint32_t { ShortcutCtrl = 1, ShortcutShift = 2, ShortcutAlt = 4 };
struct ShortcutBinding { std::uint32_t modifiers{}; std::uint32_t key{}; };
std::optional<ShortcutBinding> parseShortcut(std::wstring_view shortcut);
struct PipelineSettings {
  std::uint32_t stabilityDelayMs{180}; std::uint32_t ocrCadenceMs{120}; std::uint32_t detectorLongSide{960};
  std::uint32_t maxOcrBatch{16}; std::uint32_t maxTranslationBatch{8}; float minimumOcrConfidence{0.62F};
  ModelChoice selectedModel{ModelChoice::TranslateGemma4B};
  float lowConfidenceOpacity{0.58F}; bool showDiagnostics{true}; bool replayEnabled{true};
  std::uint32_t captureMonitorIndex{};
  std::wstring toggleShortcut{L"Ctrl+Shift+Tab"}; std::wstring originalShortcut{L"Ctrl+Alt+O"};
  std::wstring diagnosticsShortcut{L"Ctrl+Alt+D"};
  std::filesystem::path llamaExecutable; std::filesystem::path model4b; std::filesystem::path model12b;
  std::filesystem::path ocrDetector, ocrRecognizer, ocrDictionary;
  int ocrAdapter{-1}; // CPU default; nonnegative selects a DirectML adapter explicitly.
};
class SettingsStore {
 public:
  explicit SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}
  [[nodiscard]] PipelineSettings load() const;
  bool save(const PipelineSettings& settings) const;
 private: std::filesystem::path path_;
};
} // namespace d4r0
