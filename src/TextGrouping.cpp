#include "d4r0/TextGrouping.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string_view>

namespace d4r0 {
namespace {
std::string normalize(std::string_view text) {
  std::string result;
  bool pendingSpace = false;
  for (const unsigned char character : text) {
    if (std::isspace(character)) {
      pendingSpace = !result.empty();
    } else {
      if (pendingSpace) result += ' ';
      result += static_cast<char>(character);
      pendingSpace = false;
    }
  }
  return result;
}

float overlapRatio(const Rect& left, const Rect& right) {
  if (left.width <= 0 || left.height <= 0 || right.width <= 0 || right.height <= 0) return 0;
  const float width = std::max(0.0F, std::min(left.x + left.width, right.x + right.width) -
                                        std::max(left.x, right.x));
  const float height = std::max(0.0F, std::min(left.y + left.height, right.y + right.height) -
                                         std::max(left.y, right.y));
  return width * height / std::min(left.width * left.height, right.width * right.height);
}

float medianHeight(std::span<const OcrLine> lines) {
  std::vector<float> heights;
  for (const auto& line : lines)
    if (std::isfinite(line.bounds.height) && line.bounds.height > 0)
      heights.push_back(line.bounds.height);
  if (heights.empty()) return 0;
  std::sort(heights.begin(), heights.end());
  const auto middle = heights.size() / 2;
  return heights.size() % 2 ? heights[middle] : (heights[middle - 1] + heights[middle]) * 0.5F;
}

bool canAppend(const TextGroup& group, const OcrLine& line,
               const TextGroupingOptions& options, float lineHeight) {
  if (group.members.size() >= options.maxLines || line.bounds.height <= 0 || line.bounds.width <= 0) return false;
  const auto& previous = group.members.back().bounds;
  if (previous.height <= 0 || previous.width <= 0) return false;
  const float smallerHeight = std::min(previous.height, line.bounds.height);
  const float largerHeight = std::max(previous.height, line.bounds.height);
  if (smallerHeight <= 0 || largerHeight / smallerHeight > options.maxHeightRatio) return false;
  if (line.bounds.y < previous.y + previous.height * 0.5F) return false;
  const float gap = std::max(0.0F, line.bounds.y - (previous.y + previous.height));
  if (gap > options.maxVerticalGapRatio * lineHeight) return false;
  const float overlap = std::max(0.0F, std::min(previous.x + previous.width,
                                                line.bounds.x + line.bounds.width) -
                                            std::max(previous.x, line.bounds.x));
  const bool aligned = std::abs(previous.x - line.bounds.x) <= lineHeight * 0.5F ||
                       std::abs(previous.x + previous.width - line.bounds.x - line.bounds.width) <=
                           lineHeight * 0.5F;
  return aligned || overlap >= std::min(previous.width, line.bounds.width) * 0.5F;
}

void include(TextGroup& group, const OcrLine& line) {
  const float right = std::max(group.bounds.x + group.bounds.width,
                               line.bounds.x + line.bounds.width);
  const float bottom = std::max(group.bounds.y + group.bounds.height,
                                line.bounds.y + line.bounds.height);
  group.bounds.x = std::min(group.bounds.x, line.bounds.x);
  group.bounds.y = std::min(group.bounds.y, line.bounds.y);
  group.bounds.width = right - group.bounds.x;
  group.bounds.height = bottom - group.bounds.y;
  group.members.push_back(line);
  const auto text = normalize(line.text);
  if (!group.source.empty() && !text.empty()) group.source += ' ';
  group.source += text;
}
}

std::vector<TextGroup> groupTextLines(std::span<const OcrLine> lines,
                                      TextGroupingOptions options) {
  std::vector<OcrLine> ordered(lines.begin(), lines.end());
  std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
    if (left.confidence != right.confidence) return left.confidence > right.confidence;
    if (left.stableId != right.stableId) return left.stableId < right.stableId;
    if (left.bounds.y != right.bounds.y) return left.bounds.y < right.bounds.y;
    if (left.bounds.x != right.bounds.x) return left.bounds.x < right.bounds.x;
    if (left.bounds.height != right.bounds.height) return left.bounds.height < right.bounds.height;
    if (left.bounds.width != right.bounds.width) return left.bounds.width < right.bounds.width;
    return left.text < right.text;
  });
  std::vector<OcrLine> unique;
  for (const auto& line : ordered) {
    if (std::none_of(unique.begin(), unique.end(), [&](const auto& kept) {
          return overlapRatio(line.bounds, kept.bounds) >= 0.8F;
        })) unique.push_back(line);
  }
  ordered = std::move(unique);
  const float lineHeight = medianHeight(ordered);
  std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
    if (left.bounds.y != right.bounds.y) return left.bounds.y < right.bounds.y;
    if (left.bounds.x != right.bounds.x) return left.bounds.x < right.bounds.x;
    return left.stableId < right.stableId;
  });

  std::vector<TextGroup> groups;
  for (const auto& line : ordered) {
    std::size_t match = groups.size();
    for (std::size_t i = 0; i < groups.size(); ++i) {
      if (!canAppend(groups[i], line, options, lineHeight)) continue;
      if (match != groups.size()) {
        match = groups.size();
        break;
      }
      match = i;
    }
    if (match != groups.size()) {
      include(groups[match], line);
    } else {
      groups.push_back({{line}, normalize(line.text), line.bounds});
    }
  }
  return groups;
}
}
