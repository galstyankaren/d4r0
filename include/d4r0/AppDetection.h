#pragma once

#include "d4r0/TranslationProfiles.h"
#include <cstdint>
#include <optional>
#include <windows.h>

namespace d4r0 {
ProfileTemplate guessAppTemplate(const std::filesystem::path& executablePath);

class AppDetector {
 public:
  explicit AppDetector(std::uint32_t monitorIndex);
  [[nodiscard]] std::optional<AppIdentity> focusedApp() const;

 private:
  HMONITOR monitor_{};
};
} // namespace d4r0
