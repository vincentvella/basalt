// Where the application's own files are.
//
// A packaged desktop application has somewhere its build put the things it
// ships with -- a script it runs, a model it loads, a binary it drives -- and
// where that is differs by desktop. This project's packager is what decides it:
// `Foo.app/Contents/Resources` beside `Contents/MacOS/basalt_appkit` on macOS,
// and beside the executable on Linux and Windows, where there is no separate
// resources directory. See cli/packageApp.ts.
//
// So it is this platform's to answer, not the app's. An app working it out for
// itself is an app that has to know the layout of a bundle it did not build --
// which is exactly the knowledge that is supposed to live here, and which
// changes the day the packager changes.
//
// Derived from the running executable rather than asked of a toolkit.
// `NSBundle.mainBundle.resourcePath` is the authoritative answer on macOS and
// there is no equivalent on the other two, so one portable derivation beats one
// real answer and two approximations -- and the layout it derives from is this
// project's own.

#pragma once

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>

#include <memory>
#include <string>

namespace basalt {

// The directory holding what the build put beside the application, or empty
// when the running executable's path cannot be determined at all.
//
// A development run, where the host is started straight out of a build
// directory, answers that directory: there is no bundle, and the honest answer
// to "where are my resources" is "where the binary is".
std::string applicationResourcePath();

// `NativeModules.BasaltApp`, which is how JavaScript asks.
class DesktopAppModule : public facebook::react::TurboModule {
 public:
  static constexpr auto kModuleName = "BasaltApp";

  explicit DesktopAppModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

 private:
  static facebook::jsi::Value getResourcePath(
      facebook::jsi::Runtime &runtime,
      facebook::react::TurboModule &module,
      const facebook::jsi::Value *args,
      size_t count);
};

} // namespace basalt
