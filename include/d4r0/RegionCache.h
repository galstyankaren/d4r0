#pragma once
#include "d4r0/Types.h"
#include <mutex>
#include <unordered_map>

namespace d4r0 {
class RegionCache {
 public:
  struct SourceReplacement {
    std::uint64_t source{};
    std::uint64_t revision{};
    std::vector<TextRegion> regions;
  };
  // Rejects stale writes, making asynchronous OCR/translation completion safe.
  bool upsert(TextRegion region);
  void invalidateSource(std::uint64_t source, std::uint64_t revision);
  bool replaceSource(std::uint64_t source, std::uint64_t revision, std::vector<TextRegion> regions);
  bool replaceSources(std::vector<SourceReplacement> replacements);
  [[nodiscard]] std::vector<TextRegion> visible() const;
  void clear();
 private:
  mutable std::mutex mutex_; std::unordered_map<std::uint64_t, TextRegion> regions_;
  std::unordered_map<std::uint64_t, std::uint64_t> sourceRevisions_;
};
} // namespace d4r0
