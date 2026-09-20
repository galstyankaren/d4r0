#pragma once
#include <chrono>
#include <filesystem>
#include <span>
#include <cstddef>
namespace d4r0 {
// Encoder integration point. It never receives composited overlay pixels or OCR/translation data.
class ReplayBuffer {
 public:
  ReplayBuffer(); ~ReplayBuffer();
  ReplayBuffer(const ReplayBuffer&) = delete;
  [[nodiscard]] bool start(); void stop();
  void submitSourceFrame(std::span<const std::byte> encodedSourceFrame, std::chrono::steady_clock::time_point timestamp);
  [[nodiscard]] bool active() const { return active_; }
 private: std::filesystem::path directory_; bool active_{};
};
} // namespace d4r0
