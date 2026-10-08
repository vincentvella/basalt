# Upstream

Part of the [backlog](../BACKLOG.md). Not scheduled.

Everything below is a bug or a gap in somebody else's repository. Most of it has
never been sent anywhere, which is fine and is the point of writing it down, but
the two that have a decision attached are easy to lose among them. So they are
here, at the top, rather than only in the area file where each was found.

**Sent, and waiting on someone else:**

- [software-mansion/react-native-screens#4779](https://github.com/software-mansion/react-native-screens/pull/4779),
  the native-screens opt-in: `provideNativeScreens()`, so a platform that
  registers the components itself can say so. Open and mergeable, every review
  thread resolved, `kkafar` requested as reviewer, and their CI has not run yet
  because a fork pull request needs a maintainer to approve the workflow. Nothing
  is owed from this side. The reasoning lives in
  [ecosystem.md](ecosystem.md), where it was written, rather than being copied
  here.

**Found, worked around, and deliberately not sent:** entry 14 below,
`~Scheduler` leaving a mount hook registered. A one-line fix in React Native
that is not being offered, by choice. What that choice costs is carrying the
workaround and the comment explaining it, which is already written.

**Open (16):**

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
14. `~Scheduler` leaves a mount hook registered, and the next mount uses it
15. `CursorValue` is two keywords where the C++ parses thirty-four
16. `gtk_accessible_list_new_from_array` refuses every non-empty array

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

- **`~Scheduler` leaves a mount hook registered, and the next mount uses it.**
  Found 2026-10-07 from a SIGSEGV on CI's Mac, read off the pinned v0.87.1.

  `UIManager::mountHooks_` is a `std::vector<UIManagerMountHook*>`: raw,
  non-owning pointers, where taking an entry out is the owner's job and
  `~UIManagerMountHook` does not do it. `Scheduler` puts one in at
  `Scheduler.cpp:169`, `uiManager->registerMountHook(*eventPerformanceLogger_)`,
  and `~Scheduler` takes out every *commit* hook at `:197` and never that one.
  The only `unregisterMountHook` call in the whole tree is in
  `IntersectionObserverManager`.

  `ReactHost::destroyReactInstance` then frees the Scheduler, and the
  `EventPerformanceLogger` with it, three statements before it tells a mounting
  manager anything:

      stopAllSurfaces();                                   // registry will now miss
      quitSynchronous();
      surfaceManager_ = nullptr;
      scheduler_ = nullptr;                                // the hook is freed here
      schedulerDelegate_ = nullptr;
      contextContainer->erase(RuntimeSchedulerKey);
      mountingManager->setSchedulerTaskExecutor(nullptr);  // the only notice given

  A mount applied in that gap calls `reportMount`, which finds no root shadow
  node for a stopped surface and calls `shadowTreeDidUnmount` virtually on freed
  memory. The UIManager is still alive throughout, held by `UIManagerBinding` in
  a runtime destroyed later, so a `weak_ptr` to it is no protection.

  **Not specific to this repository.** Any platform that calls `reportMount`
  while an instance is going away can reach it, and iOS calls it from
  `RCTSurfacePresenter`. The fix is one line: `~Scheduler` unregistering the hook
  it registered.

  Worked around here rather than reported, which is a choice and not an
  oversight. `reportMountedSurface` in `core/UIManagerAccess.h` asks whether the
  UIManager still has the surface and refuses if not, which is sound because
  `stopAllSurfaces()` runs before the Scheduler is freed: the hook being
  dangling always implies the surface being gone. That narrows the window from
  three statements to a few instructions and does not close it. See
  [testing.md](testing.md) for the backtrace and the run it came from.

- **`CursorValue` is two keywords where the C++ parses thirty-four.**
  `Libraries/StyleSheet/StyleSheetTypes.js` declares
  `export type CursorValue = 'auto' | 'pointer'`, and that is the whole type. The
  native side is not narrow at all: `view/conversions.h` parses every CSS cursor
  keyword into the `Cursor` enum, `BaseViewProps` carries it, and iOS, macOS and
  this platform can all draw a good number of them.

  So an app that wants an I-beam over a custom text area, or a resize cursor over
  a divider, has to cast to get past Flow or TypeScript while the value works
  perfectly once it reaches C++. Found 2026-10-08 while implementing `cursor` on
  GTK and AppKit: e2e/views.tsx carries the cast and a comment pointing here.

  Worth sending, and small: the type is one line, and the list to widen it to is
  the one the C++ already accepts. Nothing here is blocked on it.

- **`gtk_accessible_list_new_from_array` refuses every non-empty array.** GTK,
  not React Native. The function allocates a `GtkAccessibleList` from an array of
  accessibles, and its guard reads

      g_return_val_if_fail (accessibles == NULL || n_accessibles == 0, NULL);

  which is the condition for an *empty* array, inverted. So the only calls it
  accepts are the ones with nothing in them: anything else gets a Gtk-CRITICAL
  and NULL back. Measured on 4.22.4 while implementing `accessibilityLabelledBy`,
  with a six-line program that calls it and `new_from_list` side by side; the
  list form has the right guard and the same effect.

  Worked around rather than waited on: `rn_view_set_labelled_by` builds a GList.
  Worth sending, and the smallest possible patch -- one operator in one guard.
  Both functions arrived together in 4.14, which suggests nobody has called the
  array one since.
