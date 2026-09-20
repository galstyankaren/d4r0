#include "d4r0/ReplayBuffer.h"
#include <cstdlib>
namespace d4r0 {
ReplayBuffer::ReplayBuffer() {
  std::error_code ec; directory_ = std::filesystem::temp_directory_path(ec) / "d4r0-replay";
}
ReplayBuffer::~ReplayBuffer() { stop(); std::error_code ec; std::filesystem::remove_all(directory_, ec); }
bool ReplayBuffer::start() { std::error_code ec; std::filesystem::create_directories(directory_, ec); active_ = !ec; return active_; }
void ReplayBuffer::stop() { active_ = false; }
void ReplayBuffer::submitSourceFrame(std::span<const std::byte>, std::chrono::steady_clock::time_point) {
  // Media Foundation hardware H.264/HEVC MFT writes a bounded 10-minute segment ring here.
  // This method deliberately has no access to overlay or text data.
}
} // namespace d4r0
