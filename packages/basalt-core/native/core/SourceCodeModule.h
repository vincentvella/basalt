// `SourceCode`, which is how JavaScript learns where the bundle came from.
//
// It sounds like diagnostics and is not: React Native's asset resolution is
// built on it. `AssetSourceResolver` asks whether the script came from a server
// or from a file, and answers `require('./logo.png')` with either a URL on that
// server or a path beside that file. With no script URL, every asset resolves
// to nonsense -- `file://assets/assets/logo.png`, in the case that led here.
//
// ReactCxxPlatform ships a SourceCode module, and on React Native 0.86 it is
// always handed an empty string: `ReactHost` has no `sourceURL_` at all, and
// 0.87 added one. Rather than support this on the versions that happen to fill
// it in, the host reports the URL itself, which it has known all along.

#pragma once

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>

#include <string>

namespace basalt {

class DesktopSourceCodeModule
    : public facebook::react::NativeSourceCodeCxxSpec<DesktopSourceCodeModule> {
 public:
  DesktopSourceCodeModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker, std::string scriptURL)
      : NativeSourceCodeCxxSpec(std::move(jsInvoker)), scriptURL_(std::move(scriptURL)) {}

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);

 private:
  std::string scriptURL_;
};

// The URL to report for a bundle loaded from `bundlePath`, or from a dev server
// when one is in use.
//
// A file URL has to be absolute and fully formed: React Native tests it with
// `startsWith('file://')` and then takes the directory beside it, so a relative
// path silently produces an asset URL that resolves nowhere.
//
// A dev server URL has to name the host's platform. React Native's HMRClient
// registers exactly this URL with Metro, and Metro keys the module graph it
// sends updates for by the platform in it. This used to say `linux` on every
// host, so on Windows Metro dutifully sent Fast Refresh updates for a Linux
// graph that the running app did not have, and no edit ever appeared.
std::string scriptURLFor(const std::string &bundlePath,
                         bool devMode,
                         const std::string &devHost,
                         unsigned int devPort,
                         const std::string &devEntry,
                         const std::string &platform);

} // namespace basalt
