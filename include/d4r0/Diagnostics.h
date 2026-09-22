#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace d4r0 {
enum class CaptureState {
  Stopped,
  Starting,
  Capturing,
  Failed,
};

enum class CropOutcome {
  NoFrame,
  Unstable,
  NoCrop,
  NoBoxes,
  OutsideCore,
  EmptyRecognition,
  LowConfidence,
  Accepted,
  Stale,
  TranslationFailure,
  ReadbackFailure,
};

struct CaptureSnapshot {
  CaptureState state{CaptureState::Stopped};
  std::uint32_t width{};
  std::uint32_t height{};
  std::uint64_t frameRevision{};
};

struct TimingSnapshot {
  double captureMs{};
  double changeDetectMs{};
  double ocrMs{};
  double translationMs{};
  double renderMs{};
  std::uint64_t droppedCapture{};
  std::uint64_t droppedOcr{};
  std::uint64_t droppedTranslation{};
  std::uint64_t staleResults{};
};

struct ResourceRow {
  std::string name;
  std::uint64_t bytes{};
  std::string detail;
};

struct CropDiagnostic {
  std::uint64_t sequence{};
  CropOutcome outcome{CropOutcome::NoFrame};
  std::uint32_t x{};
  std::uint32_t y{};
  std::uint32_t width{};
  std::uint32_t height{};
  std::vector<std::uint8_t> previewBgra;
  std::uint32_t detectorBoxes{};
  double detectorMs{};
  std::uint32_t recognizedLines{};
  double recognizerMs{};
  float confidence{};
};

struct DiagnosticsSnapshot {
  CaptureSnapshot capture;
  TimingSnapshot timing;
  std::vector<ResourceRow> resources;
  std::vector<CropDiagnostic> recentCrops;
};

class DiagnosticsStore {
public:
  void setCapture(CaptureSnapshot capture);
  void setTiming(TimingSnapshot timing);
  void updateResource(ResourceRow resource);
  void addCrop(CropDiagnostic crop);
  [[nodiscard]] DiagnosticsSnapshot snapshot() const;
  void clear();

private:
  mutable std::mutex mutex_;
  DiagnosticsSnapshot data_;
};
} // namespace d4r0
