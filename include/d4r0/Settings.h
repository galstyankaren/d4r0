#pragma once
#include "d4r0/Types.h"
#include <filesystem>

namespace d4r0 {
struct PipelineSettings {
  std::uint32_t stabilityDelayMs{180}; std::uint32_t ocrCadenceMs{120}; std::uint32_t detectorLongSide{960};
  std::uint32_t maxOcrBatch{8}; std::uint32_t maxTranslationBatch{8}; float minimumOcrConfidence{0.62F};
  float minimumTranslationConfidence{0.45F}; ModelChoice selectedModel{ModelChoice::TranslateGemma4B};
  float lowConfidenceOpacity{0.58F}; bool showDiagnostics{}; bool replayEnabled{true};
  std::uint32_t captureMonitorIndex{};
  std::wstring toggleShortcut{L"Ctrl+Shift+Tab"}; std::wstring originalShortcut{L"Ctrl+Alt+O"};
  std::filesystem::path llamaExecutable; std::filesystem::path model4b; std::filesystem::path model12b;
};
class SettingsStore {
 public:
  explicit SettingsStore(std::filesystem::path path) : path_(std::move(path)) {}
  [[nodiscard]] PipelineSettings load() const;
  bool save(const PipelineSettings& settings) const;
 private: std::filesystem::path path_;
};
} // namespace d4r0
