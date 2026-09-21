#include "d4r0/RegionCache.h"
#include <algorithm>
namespace d4r0 {
bool RegionCache::upsert(TextRegion region) {
  std::scoped_lock lock(mutex_);
  if (auto source = sourceRevisions_.find(region.sourceId);
      source != sourceRevisions_.end() && source->second > region.revision) return false;
  auto found = regions_.find(region.stableId);
  if (found != regions_.end() && found->second.revision > region.revision) return false;
  regions_.insert_or_assign(region.stableId, std::move(region)); return true;
}
std::vector<TextRegion> RegionCache::visible() const {
  std::scoped_lock lock(mutex_); std::vector<TextRegion> out; out.reserve(regions_.size());
  for (const auto& [_, region] : regions_) out.push_back(region); return out;
}
void RegionCache::invalidateSource(std::uint64_t source, std::uint64_t revision) {
  std::scoped_lock lock(mutex_);
  auto& current = sourceRevisions_[source];
  if (current > revision) return;
  current = revision;
  std::erase_if(regions_,[&](const auto& entry) { return entry.second.sourceId == source; });
}
bool RegionCache::replaceSource(std::uint64_t source, std::uint64_t revision, std::vector<TextRegion> regions) {
  std::vector<SourceReplacement> replacements;
  replacements.push_back({source,revision,std::move(regions)});
  return replaceSources(std::move(replacements));
}
bool RegionCache::replaceSources(std::vector<SourceReplacement> replacements) {
  std::scoped_lock lock(mutex_);
  for (const auto& replacement : replacements) {
    const auto found = sourceRevisions_.find(replacement.source);
    if (found != sourceRevisions_.end() && found->second > replacement.revision) return false;
  }
  for (auto& replacement : replacements) {
    sourceRevisions_[replacement.source] = replacement.revision;
    std::erase_if(regions_,[&](const auto& entry) { return entry.second.sourceId == replacement.source; });
    for (auto& region : replacement.regions) {
      region.sourceId = replacement.source; region.revision = replacement.revision;
      regions_.insert_or_assign(region.stableId,std::move(region));
    }
  }
  return true;
}
void RegionCache::clear() { std::scoped_lock lock(mutex_); regions_.clear(); sourceRevisions_.clear(); }
} // namespace d4r0
