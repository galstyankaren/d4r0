#pragma once

#include <filesystem>
#include <string_view>

namespace d4r0 {
bool initializeDebugLog(const std::filesystem::path& path);
void debugLog(std::string_view message);
}  // namespace d4r0
