// What react-native-worklets' C++ needs on a platform that is neither Android
// nor Apple.
//
// Force-included ahead of worklets' own sources; see cmake/Worklets.cmake, and
// `core/ReanimatedCompat.h` for the same arrangement one layer up.
//
// Worklets is far more portable than Reanimated -- a handful of `#ifdef`s
// rather than ninety -- but it is built and tested on Android and Apple, and
// two assumptions follow from that. Both were found by the first CI job to
// build it on Linux, which failed with eleven errors.
//
// This file is only reached on a build that is neither Android nor Apple, which
// today means Linux and, later, Windows. On macOS it compiles to nothing.

#pragma once

#if !defined(ANDROID) && !defined(__APPLE__)

// 1. The standard headers worklets uses without including.
//
// RunLoop/EventLoop.cpp calls `std::remove_if` and `std::upper_bound` while
// including <memory>, <string>, <thread>, <utility> and <vector>;
// Tools/FeatureFlags.h throws `std::logic_error`; Tools/WorkletsJSIUtils.cpp
// builds a `std::istream_iterator`. libc++ pulls each of those in behind
// something else and libstdc++ does not, so the files compile on macOS and not
// on Linux. That is a difference between standard libraries rather than
// between platforms, and it is most of the eleven errors -- four of them from
// <algorithm> alone, two for the names and two more where the compiler guessed
// `__remove_if` and then could not call the lambda handed to it.
//
// Listed as a set rather than one at a time. Each of these is a standard
// header that costs nothing to include and the alternative is finding them one
// Linux build apart, which is how the first three were found.
#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// 2. `pthread_setname_np`, which does not take the same arguments everywhere.
//
// RunLoop/AsyncQueueImpl.cpp names its thread like this:
//
//     #ifdef ANDROID
//       pthread_setname_np(pthread_self(), name.c_str());
//     #else
//       pthread_setname_np(name.c_str());
//     #endif
//
// The `#else` is Apple's one-argument form, and glibc's takes the thread as
// well -- so every non-Android platform gets Apple's spelling whether or not
// it is Apple. A one-argument overload is enough to make that call mean what
// it says, and it is an overload rather than a macro so that the two-argument
// call ten lines up still resolves to glibc's.
//
// Overloading a C function this way is legal: only one declaration may have C
// language linkage, and glibc's has it.
#include <pthread.h>

#include <cstring>

inline int pthread_setname_np(const char *name) {
  // glibc rejects anything longer than 15 characters plus the terminator with
  // ERANGE rather than truncating, and worklets' names are built as
  // `<name>(timeout)` and similar, so they reach that. Truncating keeps the
  // thread named something useful instead of leaving it unnamed; the name is
  // for a debugger and nothing reads it back.
  char shortened[16];
  const std::size_t length = std::strlen(name);
  const std::size_t keep = length < sizeof(shortened) - 1 ? length : sizeof(shortened) - 1;
  std::memcpy(shortened, name, keep);
  shortened[keep] = '\0';
  return ::pthread_setname_np(::pthread_self(), shortened);
}

#endif
