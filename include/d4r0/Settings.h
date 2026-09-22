#pragma once
#include "d4r0/Types.h"
#include <filesystem>
#include <optional>
#include <string_view>
#include <cstdint>

namespace d4r0 {
enum ShortcutModifier : std::uint32_t { ShortcutCtrl = 1, ShortcutShift = 2, ShortcutAlt = 4 };
struct ShortcutBinding { std::uint32_t modifiers{}; std::uint32_t key{}; };
std::optional<ShortcutBinding> parseShortcut(std::wstring_view shortcut);
struct PipelineSettings {
  std::uint32_t stabilityDelayMs{180}; std::uint32_t ocrCadenceMs{120}; std::uint32_t detectorLongSide{960};
  std::uint32_t maxOcrBatch{16}; std::uint32_t maxTranslationBatch{8}; float detectorThreshold{0.3F};
  float minimumOcrConfidence{0.62F}; float maxHeightRatio{1.4F}; float maxVerticalGapRatio{0.8F};
  std::uint32_t maxGroupLines{8};
  ModelChoice selectedModel{ModelChoice::TranslateGemma4B};
  float lowConfidenceOpacity{0.58F}; bool showDiagnostics{true}; bool replayEnabled{true};
  std::uint32_t captureMonitorIndex{};
  std::wstring toggleShortcut{L"Ctrl+Shift+Tab"}; std::wstring originalShortcut{L"Ctrl+Alt+O"};
  std::wstring diagnosticsShortcut{L"Ctrl+Alt+D"};
  std::filesystem::path llamaExecutable; std::filesystem::path model4b; std::filesystem::path model12b;
  std::filesystem::path ocrDetector, ocrRecognizer, ocrDictionary;
  int ocrAdapter{-1}; // CPU default; nonnegative selects a DirectML adapter explicitly.
};

enum class SettingsApply : std::uint32_t {
  None = 0, LiveDisplay = 1u << 0, Worker = 1u << 1, Pipeline = 1u << 2,
  Capture = 1u << 3, Replay = 1u << 4, Shortcuts = 1u << 5,
};
constexpr SettingsApply operator|(SettingsApply left, SettingsApply right) {
  return static_cast<SettingsApply>(static_cast<std::uint32_t>(left) |
                                    static_cast<std::uint32_t>(right));
}
constexpr bool hasSettingApply(SettingsApply value, SettingsApply flag) {
  return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}
[[nodiscard]] std::optional<std::string> validateSettings(const PipelineSettings& settings);
[[nodiscard]] SettingsApply classifySettingsChanges(const PipelineSettings& before,
                                                     const PipelineSettings& after);

class SettingsStore {
 public:
  explicit SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}
  [[nodiscard]] PipelineSettings load() const;
  bool save(const PipelineSettings& settings) const;
 private: std::filesystem::path path_;
};
} // namespace d4r0
