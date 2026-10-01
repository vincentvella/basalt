#include "SourceCodeModule.h"

#include <filesystem>

namespace basalt {

facebook::jsi::Object DesktopSourceCodeModule::getConstants(facebook::jsi::Runtime &rt) {
  facebook::jsi::Object constants(rt);
  constants.setProperty(rt, "scriptURL", facebook::jsi::String::createFromUtf8(rt, scriptURL_));
  return constants;
}

std::string scriptURLFor(const std::string &bundlePath,
                         bool devMode,
                         const std::string &devHost,
                         unsigned int devPort,
                         const std::string &devEntry,
                         const std::string &platform) {
  if (devMode) {
    // Shaped like the URL the dev server actually served, because React Native
    // recovers the server's address by stripping the path off this. Assets then
    // resolve to that server, which is what makes them work in development
    // without anything being copied anywhere. And the platform is the host's
    // own, because HMRClient registers this URL for Fast Refresh; see the
    // header.
    return "http://" + devHost + ":" + std::to_string(devPort) + "/" + devEntry +
        ".bundle?platform=" + platform + "&dev=true";
  }

  // Absolute and fully formed: React Native tests this with
  // startsWith('file://') and then takes the directory beside it, so a relative
  // path silently produces an asset URL that resolves nowhere.
  std::error_code ignored;
  const std::filesystem::path absolute = std::filesystem::absolute(bundlePath, ignored);

  // `generic_string`, not `string`: on Windows the native form is
  // `D:\build\x.js`, and `resolveAssetSource` finds the bundle's directory
  // with `scriptURL.lastIndexOf('/')`. With no forward slash in it that is -1,
  // the directory comes out empty, and every `require()`d image resolves to
  // `file://assets/...` -- a URI with an authority and no path, which opens
  // nothing. It cost a green Linux run and a red Windows one to find, and it
  // was never about LogBox: it is every asset on that platform.
  std::string path = absolute.lexically_normal().generic_string();

  // And the third slash. A POSIX path already starts with one, so `file://` and
  // `/Users/x` make `file:///Users/x`; a Windows path starts at the drive
  // letter and has to be given one, which is what every browser and URI library
  // does with `file:///C:/x`.
  if (path.empty() || path.front() != '/') {
    path.insert(path.begin(), '/');
  }
  return "file://" + path;
}

} // namespace basalt
