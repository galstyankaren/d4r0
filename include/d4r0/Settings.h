#pragma once
#include "d4r0/Types.h"
#include "d4r0/TranslationProfiles.h"
#include <filesystem>
#include <optional>
#include <string_view>
#include <cstdint>
#include <vector>

namespace d4r0 {
enum ShortcutModifier : std::uint32_t { ShortcutCtrl = 1, ShortcutShift = 2, ShortcutAlt = 4 };
struct ShortcutBinding { std::uint32_t modifiers{}; std::uint32_t key{}; };
std::optional<ShortcutBinding> parseShortcut(std::wstring_view shortcut);
struct ControllerBinding { bool raw{}; bool hid{}; std::uint32_t buttons{}; std::vector<std::uint32_t> rawButtons; };
std::optional<ControllerBinding> parseControllerBinding(std::wstring_view binding);
std::wstring controllerBindingName(const ControllerBinding& binding);
struct PipelineSettings {
  std::uint32_t stabilityDelayMs{180}; std::uint32_t ocrCadenceMs{120}; std::uint32_t detectorLongSide{960};
  std::uint32_t maxTranslationBatch{8}; float detectorThreshold{0.3F};
  float minimumOcrConfidence{0.62F}; float maxHeightRatio{1.4F}; float maxVerticalGapRatio{0.8F};
  std::uint32_t maxGroupLines{8};
  ModelChoice selectedModel{ModelChoice::TranslateGemma4B};
  float lowConfidenceOpacity{0.58F}; bool showDiagnostics{false}; bool replayEnabled{true};
  std::uint32_t captureMonitorIndex{};
  std::wstring toggleShortcut{L"Ctrl+Shift+Tab"}; std::wstring originalShortcut{L"Ctrl+Alt+O"};
  std::wstring diagnosticsShortcut{L"Ctrl+Alt+D"};
  std::wstring exitShortcut{L"Ctrl+Alt+Q"};
  std::wstring toggleControllerButton{L"None"};
  std::filesystem::path llamaExecutable; std::filesystem::path model4b; std::filesystem::path model12b;
  std::filesystem::path ocrDetector, ocrRecognizer, ocrDictionary;
  int ocrAdapter{-1}; // CPU default; nonnegative selects a DirectML adapter explicitly.
  std::vector<TranslationProfile> profiles{defaultProfiles()};
};

[[nodiscard]] std::optional<std::string> validateSettings(const PipelineSettings& settings);
[[nodiscard]] std::optional<std::string> validateShortcuts(const PipelineSettings& settings);

class SettingsStore {
 public:
  explicit SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}
  [[nodiscard]] PipelineSettings load() const;
  bool save(const PipelineSettings& settings) const;
 private: std::filesystem::path path_;
};
} // namespace d4r0
