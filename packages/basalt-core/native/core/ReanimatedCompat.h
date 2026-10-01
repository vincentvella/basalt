// What react-native-reanimated's C++ needs on a platform that is neither
// Android nor Apple.
//
// Force-included ahead of Reanimated's own sources; see cmake/Reanimated.cmake.
//
// Reanimated calls `Common/cpp` its portable half and it very nearly is -- the
// animation clock, the shadow-tree cloning, the CSS engine and the registries
// are all platform-independent. But about ninety `#ifdef ANDROID` /
// `#ifdef __APPLE__` branches run through it, and a few of them choose a *type*
// rather than an implementation:
//
//     #ifdef ANDROID
//     using SynchronouslyUpdateUIPropsFunction = ...
//     #elif __APPLE__
//     using SynchronouslyUpdateUIPropsFunction = ...
//     #endif
//
// On a third platform that alias is never declared, and `PlatformDepMethodsHolder`
// -- which has a member of that type unconditionally -- does not compile.
//
// Declaring it here first is enough: the `#elif` chain then skips a definition
// that already exists, and everything downstream sees the type it expected.
// Nothing is patched and nothing is vendored, which is the same rule the rest
// of this project keeps.
//
// The Apple shape is the one chosen, for the reason it is chosen everywhere
// else here: it takes a tag and a dynamic, which is what a desktop mounting
// manager can act on, while Android's takes two parallel arrays built for JNI.
//
// This file is only reached on a build that is neither Android nor Apple, which
// today means Linux and, later, Windows. On macOS it compiles to nothing.

#pragma once

#if !defined(ANDROID) && !defined(__APPLE__)

#include <folly/dynamic.h>

#include <functional>

namespace reanimated {

using SynchronouslyUpdateUIPropsFunction = std::function<void(const int, const folly::dynamic &)>;

} // namespace reanimated

#endif
