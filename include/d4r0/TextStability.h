#pragma once
#include "d4r0/Types.h"
#include <cstdint>
#include <string>
#include <vector>

namespace d4r0 {
// Worker-owned evidence for text on changing pixels. A candidate needs two nearby
// observations at the same location before it can be translated.
class TextStability {
 public:
  bool observe(const Rect& bounds, const std::string& text, float confidence,
               std::uint64_t nowMs);
  void clear() { tracks_.clear(); }
 private:
  struct Track { Rect bounds; std::string text; std::uint64_t seenAt{}; unsigned matches{}; };
  std::vector<Track> tracks_;
};
}
