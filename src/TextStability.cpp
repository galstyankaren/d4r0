#include "d4r0/TextStability.h"
#include <algorithm>
#include <cmath>

namespace d4r0 {
namespace {
bool scrollingContinuation(const Rect& previousBounds, const std::string& previous,
                           const Rect& bounds, const std::string& current) {
  if (previous.size() < 40 || current.size() < 40 ||
      previous.size() > 256 || current.size() > 256 ||
      previousBounds.width < previousBounds.height * 8 ||
      bounds.width < bounds.height * 8) return false;
  // A marquee exposes a shifted substring in the same narrow band. OCR can
  // change a few characters at either clipped edge, so match the interior.
  for (std::size_t oldStart = 12; oldStart + 24 <= previous.size(); ++oldStart) {
    for (std::size_t newStart = 0; newStart <= 20 && newStart + 24 <= current.size(); ++newStart) {
      if (previous.compare(oldStart, 24, current, newStart, 24) != 0) continue;
      const auto length = std::min(previous.size() - oldStart, current.size() - newStart);
      std::size_t shared = 24;
      while (shared < length && previous[oldStart + shared] == current[newStart + shared]) ++shared;
      if (previous.size() - oldStart - shared <= 20 &&
          newStart + shared + 12 <= current.size()) return true;
    }
  }
  return false;
}
}

void TextStability::prune(std::uint64_t nowMs) {
  std::erase_if(tracks_, [&](const Track& track) { return nowMs > track.seenAt + 120000; });
}

bool TextStability::observe(const Rect& bounds, const std::string& text, float confidence,
                             std::uint64_t nowMs) {
  if (bounds.width < 10 || bounds.height < 8 || bounds.width / bounds.height > 40 ||
      text.size() < 2 || confidence < 0.35F) return false;
  // Translation of a dense 4K page can take longer than the next OCR pass.
  // A changed recognition at the same position still resets its match count.
  auto isNearby = [&](const Track& track) {
    const float dx = std::abs((track.bounds.x + track.bounds.width / 2) -
                              (bounds.x + bounds.width / 2));
    const float dy = std::abs((track.bounds.y + track.bounds.height / 2) -
                              (bounds.y + bounds.height / 2));
    return dx <= std::max(16.0F, bounds.width * 0.25F) &&
           dy <= std::max(8.0F, bounds.height * 0.5F);
  };
  auto nearby = std::find_if(tracks_.begin(), tracks_.end(), [&](const Track& track) {
    return track.text == text && isNearby(track);
  });
  if (nearby == tracks_.end()) nearby = std::find_if(tracks_.begin(), tracks_.end(), isNearby);
  if (nearby == tracks_.end()) {
    tracks_.push_back({bounds, text, nowMs, 1});
    return false;
  }
  const bool consistent = nearby->text == text ||
      (nowMs >= nearby->seenAt && nowMs - nearby->seenAt <= 8000 &&
       scrollingContinuation(nearby->bounds, nearby->text, bounds, text));
  nearby->matches = consistent ? std::min(nearby->matches + 1, 3U) : 1U;
  nearby->bounds = bounds;
  nearby->text = text;
  nearby->seenAt = nowMs;
  return nearby->matches >= 2;
}
}
