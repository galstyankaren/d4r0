#pragma once
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <span>
#include <string>

namespace d4r0 {
// The caller explicitly enables this per launch. Images and OCR text are local
// diagnostic data and never enter the source-only replay buffer.
class DiagnosticSession {
 public:
  explicit DiagnosticSession(std::filesystem::path root,
                             std::uintmax_t byteLimit = 2ULL * 1024 * 1024 * 1024);
  void setEnabled(bool enabled);
  [[nodiscard]] bool enabled() const;
  [[nodiscard]] bool sourceDue(std::uint64_t nowMs, std::uint32_t intervalMs = 2000) const;
  [[nodiscard]] bool overlayDue() const;
  std::uint64_t recordSource(std::uint64_t frameRevision, std::uint64_t nowMs,
                             unsigned width, unsigned height, std::span<const std::uint8_t> bgra,
                             std::uint32_t intervalMs = 2000);
  void recordOverlay(unsigned width, unsigned height, std::span<const std::uint8_t> bgra);
  void event(const std::string& kind, std::uint64_t frameRevision, const std::string& fields = {});
  [[nodiscard]] std::filesystem::path directory() const;
  static std::string quote(const std::string& value);
 private:
  void startLocked();
  void trimCompleted();
  std::filesystem::path root_, directory_;
  std::uintmax_t byteLimit_;
  mutable std::mutex mutex_;
  std::ofstream events_;
  std::uint64_t nextImage_{}, pendingOverlay_{}, lastSourceMs_{}, lastFrameRevision_{};
  std::uintmax_t activeBytes_{};
};
}
