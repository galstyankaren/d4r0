#include "d4r0/DebugLog.h"

#include <fstream>
#include <mutex>

namespace d4r0 {
namespace {
std::mutex logMutex;
std::ofstream logFile;
}  // namespace

bool initializeDebugLog(const std::filesystem::path& path) {
  std::scoped_lock lock(logMutex);
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  if (error) return false;
  logFile.open(path, std::ios::trunc);
  return static_cast<bool>(logFile);
}

void debugLog(std::string_view message) {
  std::scoped_lock lock(logMutex);
  if (logFile) logFile << message << '\n' << std::flush;
}
}  // namespace d4r0
