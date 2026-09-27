#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace d4r0 {
struct Point { float x{}; float y{}; };
struct Rect { float x{}; float y{}; float width{}; float height{}; };
struct VisualStyle {
  float fontPx{24.0F}; bool bold{}; std::uint32_t rgba{0xFFFFFFFF};
  enum class Alignment { Left, Centre, Right } alignment{Alignment::Left};
};
struct TextRegion {
  std::uint64_t stableId{};
  std::uint64_t sourceId{};
  std::vector<Point> polygon;
  std::string german;
  float ocrConfidence{};
  std::string english;
  VisualStyle style;
  Rect bounds;
  std::uint64_t revision{};
  [[nodiscard]] bool lowConfidence(float threshold) const { return ocrConfidence < threshold; }
};
enum class ModelChoice { TranslateGemma4B, TranslateGemma12B };
enum class DisplayMode { Translation, Original };
} // namespace d4r0
