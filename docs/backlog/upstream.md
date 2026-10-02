# Upstream

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (13):**

1. `http::Body::blob` is typed `std::optional<std::string>`
2. The cxx `NetworkingModule` does not mention blobs at all
3. `BaseViewConfig` registers `onPointerDown`, `onPointerUp` and `onPointerCancel` and does not declare them
4. `ReactInstanceConfig` has no `platform` field
5. GTK 4
6. React Native's npm package omits `ReactCxxPlatform`
7. The fetch needs a git tag matching the app's React Native
8. Also worth reporting, separately and smaller: the package ships ReactCommon/re
9. Report the HttpUtils
10. `ReactCommon/cmake-utils/react-native-flags.cmake` hardcodes clang's command line
11. Report that ReactCxxPlatform's PlatformConstantsModule hardcodes a React Nativ
12. Consider upstreaming a Linux entry in getHostPlatform
13. `ImageLoaderModule` is built with no loader and nothing can supply one

- **`http::Body::blob` is typed `std::optional<std::string>`** in
  ReactCxxPlatform, and `convertRequestBody` sends `{blobId, offset, size}`,
  an object. So a `Blob` request body throws "Value is an object, expected a
  String" in the bridging layer before reaching any platform's http client,
  identically on both desktops. The fix is a structured type or bridging that
  resolves the handle.
- **The cxx `NetworkingModule` does not mention blobs at all**, so
  `responseType: 'blob'` has no response path to hook and `addNetworkingHandler`
  has nothing to register with.

- **`BaseViewConfig` registers `onPointerDown`, `onPointerUp` and
  `onPointerCancel` and does not declare them.** All three are in
  `bubblingEventTypes` with their bubbled and captured names, and supported the
  whole way down, `propsConversions.h` parses them into `ViewProps::events`,
  `PointerEventsProcessor` handles them, `TouchEventEmitter::onPointerDown`
  dispatches one. What is missing is their line in `validAttributes`, which
  lists the five hover-ish pointer props and stops. So React never sends the
  prop, the bit is never set, `shouldEmitPointerEvent` returns false, and the
  event is dropped in C++: silently, and only for the three left out.

  Costs a phone nothing, because a phone has no button to press. Costs a desktop
  the ability to answer a right-click at all, which is what found it. Worked
  around in `src/overrides/BaseViewConfig.js`, which is React Native's own file
  plus six keys and goes away when upstream adds them.

- **`ReactInstanceConfig` has no `platform` field.** `DevServerHelper` builds
  every bundle URL with `constexpr DEFAULT_PLATFORM = "android"`, so every
  desktop host asks Metro for an android bundle and the Metro plugin has to
  correct the request on arrival by reading the platform back out of `app=`.
  Worth a second upstream attempt now that
  two platforms need it rather than one.

- GTK 4.14's cairo renderer draws a transformed widget subtree unrotated and in
  the wrong colour; the GL renderer is correct. Worth reducing to a minimal case
  and reporting, or confirming it is already fixed in a later GTK.

- **React Native's npm package omits `ReactCxxPlatform`.** Worked around by
  fetching it at the app's exact version; see
  `docs/PORTING.md`. The upstream fix is one line and about
  1% of the package, and would remove the fetch entirely. Not raised: with no
  users to point at, the ask would sit. Worth revisiting when there are.
- The fetch needs a git tag matching the app's React Native. A nightly, a fork
  or an unreleased version has none, and there is no fallback.
- Also worth reporting, separately and smaller: the package ships
  `ReactCommon/react/nativemodule/cputime`'s C++ while its codegen spec lives
  under `src/private/testing/fantom` and does not ship, so that module cannot be
  compiled from the package. This platform stopped building it.
- Report the `HttpUtils.h` missing-`<cstdint>` bug. There are now two more of
  exactly the same shape and all three should go together:
  `react/renderer/components/view/conversions.h` uses `M_PI` seven times, and
  `M_PI` is a POSIX extension rather than standard C++, MSVC's `<cmath>`
  defines it only behind `_USE_MATH_DEFINES`. And
  `ReactCxxPlatform/react/runtime/ReactInstanceConfig.h` declares a `uint32_t`
  `devServerPort` while including only `<string>`.

  That third one is the best evidence the class is worth reporting rather
  than working around one at a time, because of how narrowly it shows itself.
  It breaks only on **React Native 0.86, only on Linux**: at 0.87 the same
  header also includes `<functional>` and `<memory>`, which drag `<cstdint>`
  in on libstdc++, and on a Mac libc++ supplies it either way. So the same
  Expo app built on this machine and on a CI runner disagreed about whether
  React Native compiles, and it took the release workflow's first run to say
  so. All three compile on Meta's toolchains through luck rather than intent.
- **`ReactCommon/cmake-utils/react-native-flags.cmake` hardcodes clang's command
  line** (`-Wall -Werror -fexceptions -frtti -std=c++20`) and carries
  `TODO T228344694 improve this so that it works for all platforms` directly
  beneath. Worth attaching a concrete report to: MSVC's front end has none of
  those spellings, `-Wall` is actively misread by clang-cl as `/Wall` (which it
  maps to `-Weverything`), and because `-Wpedantic` is applied `INTERFACE` on
  `callinvoker` and `react_cxxstableapi` there is no flag a consumer can add
  that lands late enough to counteract it. Phase 41 works around it by stripping
  the flags from every target after `add_subdirectory`.
- Report that `ReactCxxPlatform`'s `PlatformConstantsModule` hardcodes a React
  Native version of 1000.0.0 in every version, releases included, so nothing
  built on it can ever satisfy React Native's own development-mode version
  check. Worked around in `src/LinuxPlatformConstants.cpp`.
- Consider upstreaming a Linux entry in `getHostPlatform.js` if the host build
  ever becomes something Meta would take.

- **`ImageLoaderModule` is built with no loader and nothing can supply one.**
  `ReactCxxTurboModuleProvider::operator()` constructs it as
  `ImageLoaderModule(jsInvoker_)`, taking the default empty
  `weak_ptr<IImageLoader>`, and `ReactInstanceConfig` has no field for one. So
  `Image.getSize` and `Image.prefetch` cannot work on any ReactCxxPlatform host
  as shipped: the promise never settles.

  The interface exists and is small (`loadImage` and `getCacheStatus`) so
  the fix upstream is a config field and one more constructor argument,
  alongside the ones already there for the WebSocket client factory and the
  dev UI delegate.

  Worked around here by building the module in each host's own provider, which
  is consulted first. That is a supported seam rather than a trick, but it
  means every host has to know to do it.
