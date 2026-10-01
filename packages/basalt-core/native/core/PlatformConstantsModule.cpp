#include "PlatformConstantsModule.h"

#include <cstdlib>

#ifndef BASALT_REACT_NATIVE_VERSION
// Only reachable if the build did not pass one, which CMake always does.
#define BASALT_REACT_NATIVE_VERSION "0.0.0"
#endif

namespace basalt {

using facebook::react::PlatformConstantsAndroid;

DesktopPlatformConstantsModule::Version DesktopPlatformConstantsModule::parseVersion(
    const std::string &text) {
  Version version;

  // A prerelease is everything after the first '-', and it must not be parsed
  // as part of the patch number: "0.88.0-rc.2" is patch 0, prerelease "rc.2".
  const auto dash = text.find('-');
  const std::string numbers = text.substr(0, dash);
  if (dash != std::string::npos && dash + 1 < text.size()) {
    version.prerelease = text.substr(dash + 1);
  }

  int *fields[] = {&version.major, &version.minor, &version.patch};
  std::size_t start = 0;
  for (int index = 0; index < 3 && start <= numbers.size(); ++index) {
    const auto dot = numbers.find('.', start);
    const std::string part =
        numbers.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
    *fields[index] = std::atoi(part.c_str());
    if (dot == std::string::npos) {
      break;
    }
    start = dot + 1;
  }

  return version;
}

PlatformConstantsAndroid DesktopPlatformConstantsModule::getConstants(facebook::jsi::Runtime &rt) {
  // Everything but the version is what ReactCxxPlatform reports, kept in step
  // with it deliberately: this class exists to correct one field, not to invent
  // a different set of constants.
  facebook::react::PlatformConstantsModule base{jsInvoker_};
  PlatformConstantsAndroid constants = base.getConstants(rt);

  const Version version = parseVersion(BASALT_REACT_NATIVE_VERSION);
  constants.reactNativeVersion.major = version.major;
  constants.reactNativeVersion.minor = version.minor;
  constants.reactNativeVersion.patch = version.patch;
  // Left absent deliberately. The Android spec this platform answers types
  // `prerelease` as an optional *int*, so a tag like "rc.2" has nowhere to go;
  // reporting nothing is at least not a lie, and a stable release has none
  // anyway. The tag is still parsed off, above, so that it cannot be read as
  // part of the patch number.
  constants.reactNativeVersion.prerelease = std::nullopt;

  return constants;
}

} // namespace basalt
