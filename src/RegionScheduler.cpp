#include "d4r0/RegionScheduler.h"
#include <stdexcept>

namespace d4r0 {
void RegionScheduler::reset(std::size_t count) {
  tiles_.assign(count, {});
  for (auto& tile : tiles_) tile.revision = ++nextRevision_;
  cursor_ = 0;
}
void RegionScheduler::observe(std::span<const std::uint32_t> changes, std::uint64_t now) {
  if (changes.size() != tiles_.size()) throw std::invalid_argument("Tile grid changed without scheduler reset");
  for (std::size_t i = 0; i < changes.size(); ++i) {
    if (!changes[i]) continue;
    tiles_[i] = {++nextRevision_, now, true, false};
  }
}
std::vector<RegionJob> RegionScheduler::takeReady(std::uint64_t now, std::uint32_t delay, std::size_t limit) {
  std::vector<RegionJob> jobs;
  if (tiles_.empty() || !limit) return jobs;
  for (std::size_t visited = 0; visited < tiles_.size() && jobs.size() < limit; ++visited) {
    const auto index = cursor_;
    cursor_ = (cursor_ + 1) % tiles_.size();
    auto& tile = tiles_[index];
    if (tile.dirty && !tile.inFlight && now >= tile.changedAt && now-tile.changedAt >= delay) {
      tile.inFlight = true;
      jobs.push_back({index,tile.revision,++tile.attempt});
    }
  }
  return jobs;
}
bool RegionScheduler::current(RegionJob job) const {
  return job.tile < tiles_.size() && tiles_[job.tile].revision == job.revision &&
         tiles_[job.tile].attempt == job.attempt;
}
bool RegionScheduler::complete(RegionJob job) {
  if (!current(job) || !tiles_[job.tile].inFlight) return false;
  tiles_[job.tile].dirty = false;
  tiles_[job.tile].inFlight = false;
  return true;
}
void RegionScheduler::retry(RegionJob job, std::uint64_t retryAtMs) {
  if (current(job)) {
    tiles_[job.tile].inFlight = false;
    tiles_[job.tile].changedAt = retryAtMs;
  }
}
}
