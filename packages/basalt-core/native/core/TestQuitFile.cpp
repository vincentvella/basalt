#include "TestQuitFile.h"

#include <cstdlib>
#include <filesystem>
#include <optional>
#include <string>

namespace basalt {

std::optional<std::string> testQuitFilePath() {
  static const std::optional<std::string> path = []() -> std::optional<std::string> {
    const char *value = std::getenv(kTestQuitFileVar);
    if (value == nullptr || *value == '\0') {
      return std::nullopt;
    }
    return std::string(value);
  }();
  return path;
}

bool testQuitFileAppeared() {
  const std::optional<std::string> path = testQuitFilePath();
  if (!path.has_value()) {
    return false;
  }
  // The non-throwing overload. This runs on a timer on the UI thread, and a
  // permission error or a vanished parent directory is a reason to keep waiting
  // rather than to terminate the process from inside a timer callback.
  std::error_code ignored;
  return std::filesystem::exists(*path, ignored);
}

} // namespace basalt
