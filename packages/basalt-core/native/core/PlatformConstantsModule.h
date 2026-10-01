// The `PlatformConstants` TurboModule, replacing ReactCxxPlatform's.
//
// ReactCxxPlatform ships one, and it hardcodes a React Native version of
// 1000.0.0 -- the placeholder `main` uses for "not a release" -- in every
// version, releases included. React Native's JavaScript compares that against
// the version the bundle was built from and, in a development build, reports
//
//     React Native version mismatch.
//     JavaScript version: 0.87.1
//     Native version: 1000.0.0
//
// which is a `console.error`, so it is noise rather than a crash, but it is
// noise that every developer on a released version would see forever, and it
// tells them to do things that will not help. So this reports the version the
// host was actually built against, which CMake reads out of React Native's own
// package.json and passes in as BASALT_REACT_NATIVE_VERSION.
//
// Worth reporting upstream: nothing in ReactCxxPlatform can match a release
// while that constant is baked in.
//
// Everything else is copied from ReactCxxPlatform's implementation, including
// the Android-shaped fields, because that is the spec this platform answers.
// See packages/basalt-core/src/overrides/createPlatform.js for why the
// Android spec is the right one here.

#pragma once

#include <react/coremodules/PlatformConstantsModule.h>

#include <string>

namespace basalt {

// Note the base class: the generated spec binds each method to the class it is
// templated on, at compile time, so subclassing ReactCxxPlatform's module and
// overriding getConstants does nothing at all -- the method table still points
// at the base. It has to derive from the spec itself. That failure is silent
// and looks exactly like the provider not being consulted.
class DesktopPlatformConstantsModule
    : public facebook::react::NativePlatformConstantsAndroidCxxSpec<DesktopPlatformConstantsModule> {
 public:
  explicit DesktopPlatformConstantsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativePlatformConstantsAndroidCxxSpec(std::move(jsInvoker)) {}

  std::string getAndroidID(facebook::jsi::Runtime & /*rt*/) {
    return "";
  }

  facebook::react::PlatformConstantsAndroid getConstants(facebook::jsi::Runtime &rt);

  // Parses "0.87.1" or "0.88.0-rc.2". Exposed for the tests, which is the only
  // way to be sure a prerelease tag does not silently become part of the patch
  // number. The tag itself is not reported: the Android spec types it as an
  // optional int, so "rc.2" cannot be expressed.
  struct Version {
    int major{0};
    int minor{0};
    int patch{0};
    std::string prerelease;
  };
  static Version parseVersion(const std::string &text);
};

} // namespace basalt
