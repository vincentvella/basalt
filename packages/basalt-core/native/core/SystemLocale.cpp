#include "SystemLocale.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <mutex>

namespace basalt {

namespace {

struct State {
  std::mutex mutex;
  SystemLocale locale;
  bool allowRTL{true};
  std::optional<bool> forced;
};

State &state() {
  static State value;
  return value;
}

// The scripts that read right to left, by language code. Short because it is
// short: Arabic, Hebrew, Persian, Urdu and the two Kurdish and Pashto codes
// cover what a test needs, and nothing here decides a real locale -- the
// platforms answer that themselves. See the header.
bool languageReadsRightToLeft(const std::string &identifier) {
  std::string language;
  for (const char letter : identifier) {
    if (letter == '-' || letter == '_' || letter == '.') {
      break;
    }
    language += static_cast<char>(std::tolower(static_cast<unsigned char>(letter)));
  }
  return language == "ar" || language == "he" || language == "iw" || language == "fa" ||
      language == "ur" || language == "ps" || language == "ckb" || language == "yi";
}

} // namespace

std::optional<SystemLocale> testLocale() {
  static const std::optional<SystemLocale> requested = []() -> std::optional<SystemLocale> {
    const char *const value = std::getenv("BASALT_TEST_LOCALE");
    if (value == nullptr || *value == '\0') {
      return std::nullopt;
    }
    SystemLocale locale;
    locale.identifier = value;
    locale.rightToLeft = languageReadsRightToLeft(locale.identifier);
    return locale;
  }();
  return requested;
}

void setSystemLocale(SystemLocale locale) {
  State &shared = state();
  const std::lock_guard<std::mutex> guard(shared.mutex);
  shared.locale = std::move(locale);
}

SystemLocale lastKnownSystemLocale() {
  if (const std::optional<SystemLocale> requested = testLocale()) {
    return *requested;
  }
  State &shared = state();
  const std::lock_guard<std::mutex> guard(shared.mutex);
  return shared.locale;
}

bool resolveRightToLeft(bool allowRTL, std::optional<bool> forced, bool systemRightToLeft) {
  if (!allowRTL) {
    return false;
  }
  if (forced.has_value()) {
    return *forced;
  }
  return systemRightToLeft;
}

bool layoutIsRightToLeft() {
  const SystemLocale locale = lastKnownSystemLocale();
  State &shared = state();
  const std::lock_guard<std::mutex> guard(shared.mutex);
  return resolveRightToLeft(shared.allowRTL, shared.forced, locale.rightToLeft);
}

void setAllowRTL(bool allow) {
  State &shared = state();
  const std::lock_guard<std::mutex> guard(shared.mutex);
  shared.allowRTL = allow;
}

void setForceRTL(bool force) {
  State &shared = state();
  const std::lock_guard<std::mutex> guard(shared.mutex);
  // `forceRTL(false)` clears the force rather than forcing left-to-right,
  // which is what React Native's own platforms do: an app turning it off goes
  // back to following the system.
  shared.forced = force ? std::optional<bool>(true) : std::nullopt;
}

} // namespace basalt
