#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <d3d11.h>
namespace d4r0 {
// Accepts only source-capture GPU textures. It has no overlay or text API.
// Twenty 30-second H.264 segments form the private 10-minute ring.
class ReplayBuffer {
 public:
  ReplayBuffer();
  ~ReplayBuffer();
  ReplayBuffer(const ReplayBuffer&) = delete;
  [[nodiscard]] bool start();
  void stop();
  void submitSourceFrame(ID3D11Texture2D* source, std::chrono::steady_clock::time_point timestamp);
  [[nodiscard]] bool active() const;
  [[nodiscard]] std::wstring lastError() const;
  [[nodiscard]] const std::filesystem::path& directory() const;
 private:
  struct State;
  std::unique_ptr<State> state_;
};
} // namespace d4r0
