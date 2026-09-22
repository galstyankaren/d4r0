#pragma once

#include "d4r0/Types.h"
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace d4r0 {
struct OcrLine {
  std::uint64_t stableId{};
  Rect bounds;
  float confidence{};
  std::string text;
};

struct TextGroup {
  std::vector<OcrLine> members;
  std::string source;
  Rect bounds;
};

struct TextGroupingOptions {
  float maxHeightRatio{1.4F};
  float maxVerticalGapRatio{0.8F};
  std::size_t maxLines{8};
};

[[nodiscard]] std::vector<TextGroup> groupTextLines(
    std::span<const OcrLine> lines, TextGroupingOptions options = {});
}
