#pragma once
#include "d4r0/Types.h"
#include <mutex>
#include <unordered_map>

namespace d4r0 {
class RegionCache {
 public:
  // Rejects stale writes, making asynchronous OCR/translation completion safe.
  bool upsert(TextRegion region);
  [[nodiscard]] std::vector<TextRegion> visible() const;
  void clear();
 private:
  mutable std::mutex mutex_; std::unordered_map<std::uint64_t, TextRegion> regions_;
};
} // namespace d4r0
