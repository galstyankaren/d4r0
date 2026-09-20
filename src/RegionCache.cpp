#include "d4r0/RegionCache.h"
namespace d4r0 {
bool RegionCache::upsert(TextRegion region) {
  std::scoped_lock lock(mutex_);
  auto found = regions_.find(region.stableId);
  if (found != regions_.end() && found->second.revision > region.revision) return false;
  regions_.insert_or_assign(region.stableId, std::move(region)); return true;
}
std::vector<TextRegion> RegionCache::visible() const {
  std::scoped_lock lock(mutex_); std::vector<TextRegion> out; out.reserve(regions_.size());
  for (const auto& [_, region] : regions_) out.push_back(region); return out;
}
void RegionCache::clear() { std::scoped_lock lock(mutex_); regions_.clear(); }
} // namespace d4r0
