// Expo's runtime, installed into the JavaScript runtime before the bundle runs.
//
// An Expo app's very first import reaches `globalThis.expo.EventEmitter`, and
// without it the app dies before React renders. That object is normally
// installed from native code, which is why an Expo app cannot start on a
// platform nobody has ported Expo to.
//
// The port is smaller than it sounds, because expo-modules-core ships the
// portable half in its npm package: `common/cpp` holds EventEmitter,
// SharedObject, SharedRef, NativeModule and their JSI helpers, and depends on
// nothing but JSI and a call invoker. So there is nothing to vendor and nothing
// to fetch -- this compiles the app's own copy, at the app's own version, the
// same arrangement that keeps React Native's C++ and JavaScript in step.
//
// react-native-windows and react-native-macos reach the same place by way of
// shirakaba/expo-desktop, which vendors that directory at a pinned Expo SDK.
// Compiling the app's copy avoids the pin.
//
// What this installs is deliberately the floor rather than the ceiling: the
// four classes and an empty module registry, which is what an Expo app needs to
// *start*. Any actual Expo module -- expo-font, expo-image, a config plugin's
// native half -- needs its own port, and this does not pretend otherwise.

#pragma once

#include <jsi/jsi.h>

namespace basalt {

// Installs `globalThis.expo`. Call before the bundle is evaluated; ReactHost's
// bindingsInstallFunc is that moment.
//
// Compiled only when the build was pointed at an expo-modules-core; without one
// this is a no-op, so a plain React Native app pays nothing and an Expo app
// fails the way it did before rather than differently.
void installExpoRuntime(facebook::jsi::Runtime &runtime);

// Whether the above will do anything, for the log line that tells a developer
// which of the two situations they are in.
bool hasExpoRuntime();

} // namespace basalt
