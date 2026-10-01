// How a capability that does not live in core reaches the runtime.
//
// The host is one binary. There is no dynamic native module loading here, so a
// capability shipped as its own package is still *compiled in* -- which means
// the question is not how it loads but how core learns it exists without
// naming it.
//
// It does not. CMake writes the answer. When the build discovers a package that
// declares native code, it generates a translation unit defining the function
// below to call that package's installer; when it discovers none, it generates
// one with an empty body. Core calls it either way and never mentions a
// package.
//
// Generated rather than registered at static initialisation, which is the
// obvious alternative and the wrong one here: a registrar in a static library
// is only linked in if something already references its translation unit, so
// the capability would vanish from a release build and stay in a debug one.
// That failure is silent and platform-specific, which is the worst shape a
// build problem can have.

#pragma once

#include <ReactCommon/CallInvoker.h>
#include <ReactCommon/TurboModule.h>

#include <jsi/jsi.h>

#include <memory>
#include <string>

namespace basalt {

// Sets each discovered package's Expo modules on `modules`, which is
// `globalThis.expo.modules`. Defined by the generated translation unit; see
// cmake/BasaltPackages.cmake.
void installPackageExpoModules(facebook::jsi::Runtime &runtime, facebook::jsi::Object &modules);

// A discovered package's TurboModule, or nullptr when no package supplies one by
// that name. Defined by the same generated translation unit.
//
// The signature is React Native's own `TurboModuleProvider`, so a host adds this
// to its list of providers directly rather than wrapping it -- which is the
// whole of the wiring, and why a package contributing a TurboModule costs a host
// nothing. A package that wants one names a factory of this shape in its
// CMakeLists; see basalt-subprocess.
std::shared_ptr<facebook::react::TurboModule> packageTurboModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker);

} // namespace basalt
