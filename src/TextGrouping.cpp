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
  if (gap > std::min(options.maxVerticalGapRatio * lineHeight,
                     smallerHeight * 0.5F)) return false;
  const float overlap = std::max(0.0F, std::min(previous.x + previous.width,
                                                line.bounds.x + line.bounds.width) -
                                            std::max(previous.x, line.bounds.x));
  const bool aligned = std::abs(previous.x - line.bounds.x) <= lineHeight * 0.5F ||
                       std::abs(previous.x + previous.width - line.bounds.x - line.bounds.width) <=
                           lineHeight * 0.5F;
  const float groupRight = group.bounds.x + group.bounds.width;
  const float lineRight = line.bounds.x + line.bounds.width;
  const auto lastCharacter=group.members.back().text.find_last_not_of(" \t\r\n");
  const bool wrappedWord=lastCharacter!=std::string::npos &&
      group.members.back().text[lastCharacter]=='-' &&
      line.bounds.x >= group.bounds.x-lineHeight*3.0F;
  const bool withinColumn = line.bounds.x >= group.bounds.x -
                                (wrappedWord ? lineHeight*3.0F : lineHeight) &&
                            lineRight <= groupRight + std::max(lineHeight, group.bounds.width * 0.5F);
  return (aligned || wrappedWord) && withinColumn &&
         overlap >= std::min(previous.width, line.bounds.width) * 0.5F;
}

bool startsSeparateComponent(const TextGroup& group, const OcrLine& line,
                             std::span<const OcrLine> following) {
  const auto& previous=group.members.back().bounds;
  const auto gap=line.bounds.y-(previous.y+previous.height);
  const auto smallerHeight=std::min(previous.height,line.bounds.height);
  if (group.members.size()>1)
    return previous.height >= line.bounds.height*1.2F && gap > smallerHeight*0.25F;
  if (line.bounds.height >= previous.height*1.2F && gap > smallerHeight*0.15F &&
      line.bounds.width >= previous.width*1.2F) return true;
  if (previous.height >= line.bounds.height*1.13F &&
      line.bounds.width >= previous.width*1.2F) return true;
  if (gap <= smallerHeight*0.2F) return false;
  for (const auto& next:following) {
    if (next.bounds.y <= line.bounds.y ||
        std::abs(next.bounds.x-line.bounds.x) > line.bounds.height*0.5F) continue;
    const auto nextGap=next.bounds.y-(line.bounds.y+line.bounds.height);
    if (nextGap < 0 || nextGap > line.bounds.height*0.5F) continue;
    return nextGap*2 < gap;
  }
  return false;
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
  if (!group.source.empty() && !text.empty()) {
    if (group.source.back()=='-' && std::islower(static_cast<unsigned char>(text.front())))
      group.source.pop_back();
    else group.source += ' ';
  }
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
  for (std::size_t lineIndex=0;lineIndex<ordered.size();++lineIndex) {
    const auto& line=ordered[lineIndex];
    std::size_t match = groups.size();
    for (std::size_t i = 0; i < groups.size(); ++i) {
      if (!canAppend(groups[i], line, options, lineHeight)) continue;
      if (startsSeparateComponent(groups[i],line,
          std::span<const OcrLine>(ordered).subspan(lineIndex+1))) continue;
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
