#pragma once

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace d4r0 {
struct Point { float x{}; float y{}; };
struct Rect { float x{}; float y{}; float width{}; float height{}; };
struct VisualStyle {
  float fontPx{24.0F}; bool bold{}; std::uint32_t rgba{0xFFFFFFFF};
  bool outlined{}; bool shadow{}; enum class Alignment { Left, Centre, Right } alignment{Alignment::Left};
};
struct TextRegion {
  std::uint64_t stableId{};
  std::vector<Point> polygon;
  std::string german;
  float ocrConfidence{};
  std::string english;
  float translationConfidence{};
  VisualStyle style;
  Rect bounds;
  std::uint64_t revision{};
  bool backgroundSafe{};
  [[nodiscard]] bool lowConfidence(float threshold) const { return ocrConfidence < threshold; }
};
enum class ModelChoice { TranslateGemma4B, TranslateGemma12B };
enum class DisplayMode { Translation, Original };
struct PerformanceSnapshot {
  double captureMs{}; double changeDetectMs{}; double ocrMs{}; double translateMs{}; double renderMs{};
  double frameMs{}; std::uint64_t gpuBytes{}; std::uint32_t droppedCapture{}; std::uint32_t droppedOcr{};
  std::uint32_t droppedTranslation{}; bool replayActive{}; ModelChoice activeModel{ModelChoice::TranslateGemma4B};
};
} // namespace d4r0
