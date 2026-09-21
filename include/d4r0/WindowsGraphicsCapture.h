#pragma once

#include <cstdint>
#include <d3d11.h>
#include <mutex>
#include <memory>
#include <windows.h>
#include <wrl/client.h>

namespace d4r0 {
struct CapturedFrame {
  Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
  std::uint64_t revision{};
};

class WindowsGraphicsCapture {
 public:
  explicit WindowsGraphicsCapture(std::uint32_t monitorIndex) : monitorIndex_(monitorIndex) {}
  ~WindowsGraphicsCapture();
  WindowsGraphicsCapture(const WindowsGraphicsCapture&) = delete;

  bool start();
  void stop();
  [[nodiscard]] CapturedFrame latestFrame() const;

 private:
  std::uint32_t monitorIndex_{};
  mutable std::mutex mutex_;
  struct State;
  std::shared_ptr<State> state_;
};
}  // namespace d4r0
