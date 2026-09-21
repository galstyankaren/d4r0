#pragma once
#include <cstdint>
#include <span>
#include <vector>

namespace d4r0 {
struct RegionJob { std::size_t tile{}; std::uint64_t revision{}, attempt{}; };
// Single pipeline-worker owner; times are monotonic milliseconds.
class RegionScheduler {
 public:
  void reset(std::size_t tileCount);
  void observe(std::span<const std::uint32_t> changedPixels, std::uint64_t nowMs);
  std::vector<RegionJob> takeReady(std::uint64_t nowMs, std::uint32_t stabilityMs, std::size_t maxBatch);
  [[nodiscard]] bool current(RegionJob job) const;
  [[nodiscard]] std::uint64_t revision(std::size_t tile) const { return tiles_.at(tile).revision; }
  [[nodiscard]] bool matches(std::size_t tile, std::uint64_t revision) const {
    return tile < tiles_.size() && tiles_[tile].revision == revision;
  }
  bool complete(RegionJob job);
  void retry(RegionJob job, std::uint64_t retryAtMs = 0);
 private:
  struct Tile { std::uint64_t revision{}, changedAt{}; bool dirty{true}, inFlight{}; std::uint64_t attempt{}; };
  std::vector<Tile> tiles_;
  std::uint64_t nextRevision_{};
  std::size_t cursor_{};
};
}
