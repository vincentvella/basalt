# Compatibility

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (7):**

1. React Native 0.82 and older cannot reach a native module
2. React Native 0.83 through 0.86 are refused rather than untested-but-allowed
3. Supporting React Native 0.81, which is what kino pins
4. CI checks one React Native per run
5. Expo starts but no Expo module works
6. ~~No @shopify/react-native-skia, which stops one real app dead~~, done on
   all three hosts, for the 2.x line
7. The version this host reports cannot express a prerelease tag
8. React Native Skia 3.x is Graphite over Dawn, and this platform is Ganesh

Found by bundling and running a real application.

- **React Native 0.82 and older cannot reach a native module.** The host
  installs `global.nativeModuleProxy` but not `global.__turboModuleProxy`, and
  relies on `TurboModuleRegistry` falling back to `NativeModules`. On `main` and
  on releases from 0.83 that fallback is unconditional; on 0.82 and earlier,
  which includes kino's 0.81.6, it is gated behind
  `RN$Bridgeless !== true || RN$TurboInterop === true ||
  RN$UnifiedNativeModuleProxy === true`, all of which are false here. So every
  `getEnforcing` fails on those versions, starting with `PlatformConstants`.
  Setting
  `globalThis.RN$TurboInterop = true` before the bundle evaluates fixes it and
  is verified; installing `__turboModuleProxy` is the better answer and belongs
  upstream in ReactCxxPlatform.

  0.87.1 works without any of this, so the supported range starts there; see
  `docs/PORTING.md`. Extending it downwards means testing each version, not just
  setting the flag.

  This entry briefly claimed the flag was needed on `main` too, on the strength
  of `getEnforcing('RNSkiaModule')` failing without it and succeeding with it
  while bundling `e2e/skia.tsx`. That was wrong and the reasoning was bad: the two
  runs differed in another way as well: React was not yet pinned, so Skia had
  its own copy, and once it was pinned the bundle works **with or without the
  flag**. `TurboInterop` does not appear anywhere in this React Native, so the
  flag had no referent and could not have been what changed. Two variables, one
  conclusion, and the wrong one.

  Why it works here without any of it: `TurboModuleBinding::install` installs
  `__turboModuleProxy` only when `RN$Bridgeless` is absent, and installs
  `nativeModuleProxy` otherwise. This platform is bridgeless, so it gets the
  second, and `TurboModuleRegistry` reaches it through the ungated
  `NativeModules[name]` fallback. That is upstream's design rather than a gap.
  What 0.82 and earlier gate is that fallback, which is what the paragraph above
  is about and why installing `__turboModuleProxy` ourselves would help *there*;
   path one would succeed and the gate would never be reached.
- React Native 0.83 through 0.86 are refused rather than untested-but-allowed.
  Each would need building against and both suites run; see
  `docs/PORTING.md` and `supported-versions.json`.
- Supporting React Native 0.81, which is what kino pins, needed three separate
  things. **The first is done.** `core/TurboModuleProxy.h` installs
  `globalThis.__turboModuleProxy` as an alias for `nativeModuleProxy[name]`, so
  `TurboModuleRegistry`'s first question is answerable and the gated fallback is
  never reached. Verified against 0.81's own JavaScript rather than by reading
  it: kino's unmodified bundle, with no flag prepended, no longer fails at
  `getEnforcing('Networking')` and reaches `Running "main"`. The gate is in the
  JavaScript, so a bundle built from 0.81 tests 0.81 whatever the host was built
  against.

  **"three separate things" was wrong, and so was the list.** The authoritative
  account is `supported-versions.json`, under `known-unsupported`, and it counts
  six differences rather than three. The Hermes naming is not one of the
  remaining ones: `ThirdParty.cmake` already takes either library name and
  defines both `hermes-engine::hermesvm` and `hermes-engine::libhermes`, with a
  comment saying it is for 0.81. Checked rather than assumed, after this entry
  named it as outstanding twice.

  Three of the six are handled: the module proxy above, the Hermes naming, and
  0.81's codegen. The three that remain are the structural ones, and they are
  why that file's verdict is what it is:

  - **`ReactHost`'s constructor differs**: eleven parameters rather than twelve,
    `TurboModuleManagerDelegates` rather than `TurboModuleProviders`, and no
    choreographer. A second host construction path, permanently.
  - **There is no websocket client.** 0.81 ships `IWebSocketClient` with nothing
    behind it, so no packager connection, no Fast Refresh, and no websockets for
    the app. The largest piece.
  - `GtkAnimationChoreographer` has to be compiled out: 0.81 has
    `react/renderer/animations` and not `react/renderer/animationbackend`.

  So the 0.81-versus-upgrade decision leans harder towards upgrading than this
  entry has been implying, and `supported-versions.json` already said as much:
  *"worth doing only for a specific app that needs it, and kino would still stop
  at Expo afterwards."*
- **CI checks one React Native per run.** It pins 0.87.1, and `main` moved to
  the weekly drift job, which does not block. So a regression that only affects
  `main` can wait up to a week. Building both on every run would be the fix and
  would double CI cost on a private repo.

  The reverse also happens, and it is less obvious: a check can pass against
  `main` and fail against the pin. The e2e type check did, on all nine shards,
  the first time it ran.

  The cause is not the version as such, it is which of the three files in
  build_ts.sh's list gets picked. A developer's checkout has usually generated
  `types_generated/`, whose tree pulls in `src/types/globals.d.ts` -- and that
  file hand-declares `Blob`, `File`, `FileReader`, `requestAnimationFrame` and
  `requestIdleCallback`. A release checkout has no `types_generated/`, so the
  fallback is `ReactNativeApi.d.ts`, a flat API snapshot that declares no
  globals and references nothing. `src/types/globals.d.ts` is still sitting in
  that checkout, 0.87.1 included; nothing reaches it. So `@types/node`'s
  `buffer` Blob wins and the other four are undeclared.

  Anything that merges into one of those ambient names is therefore dependent on
  how the checkout was prepared, not just on its version. e2e/runtime.ts is the
  answer for the demo apps: it names the surface the *runtime* installs and
  reaches it through one cast of `globalThis`, so no ambient declaration has to
  win.

  The `typecheck` job in ci.yml is the other half, and the reason it is a job
  rather than a line in the build ones: it runs against a release checkout, so
  it exercises the `ReactNativeApi.d.ts` path that nobody's own machine does.
- **Expo starts but no Expo module works.** `globalThis.expo` is installed now, from the
  app's own expo-modules-core, so an Expo app starts and renders. What does not
  exist is any Expo *module*: `ExpoAsset` and `ExponentConstants` are stubs that
  exist only so Expo's start-up survives, and `expo-font`, `expo-image` and
  every config plugin's native half are each their own port.
- ~~**No `@shopify/react-native-skia`**~~, which was the thing that stopped kino dead,
  and it is closer than "not this platform's to fix" suggested. Established by
  bundling kino's `apps/desktop` against basalt and running it, 2026-09-26:

  The bundle builds with no errors at all: Metro resolves kino's whole tree,
  Expo included, through `withDesktopPlatforms`. The app then fails at
  `TurboModuleRegistry.getEnforcing('Networking')`, which is the 0.81 gate above
  and nothing to do with Skia. With `globalThis.RN$TurboInterop = true`
  prepended, Networking resolves, `FormData` stops being missing, the app reaches
  `Running "main"`, and the next and only blocker is
  `getEnforcing('RNSkiaModule')`. So the version gate is one line and Skia is the
  whole remaining distance.

  What Skia actually needs here, from reading what it ships:

  - **Skia itself is not the problem.** `libs/macos` holds prebuilt binaries and
    `cpp/{api,jsi,rnskia}` is host-agnostic C++ over JSI.
  - **`RNSkiaModule` is 61 lines.** One `RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD(install)`
    that builds a `SkiaManager` from an `RCTBridge` and a `CallInvoker`, and
    `SkiaManager` wraps `RNSkia::RNSkManager`. basalt has a runtime and a call
    invoker; what it lacks is an `RNSkPlatformContext`, and
    `RNSkApplePlatformContext` exists and is Metal and CoreGraphics -- so this is
    adapting the places it reaches for `RCTBridge`, not writing it.
  - **`<Canvas>` is the real work**, and it is separable. `SkiaPictureView` and
    `SkiaUIView` are written against `RCTViewComponentView`, so an AppKit host
    needs its own view hosting a `CAMetalLayer` plus the component registered in
    `AppKitMountingManager`.

  Which stages: the module alone unblocks Skia's imperative API: `Skia.Path`,
  image decoding, typefaces, which is what kino's `runtime/images.ts`,
  `runtime/media.ts` and `store.ts` use. `<Canvas>` is only needed for
  `ui/Preview.tsx` and `ui/AnchorsPane.tsx`.

  GTK and Win32 were a different proposition: no prebuilt Skia, and a GL, Vulkan
  or D3D window context rather than Metal. Both are done -- Skia built from
  source by `scripts/build_skia_linux.sh` and its Windows counterpart, and a
  raster canvas on each, which is a CPU surface handed over as a `GdkTexture` or
  blitted -- so what staged above as "`<Canvas>` is the real work" is finished on
  all three. The next entry is what replaces this one.
- **React Native Skia 3.x is Graphite over Dawn, and this platform is Ganesh.**
  Measured against `react-native-skia@3.4.0`, published 2026-10-11, on
  2026-10-11. This is not a version to catch up to; it is a second renderer, and
  the host half above does not survive it:

  - **Six of the seven Apple sources `cmake/Skia.cmake` borrows are deleted.**
    `MetalContext`, `MetalWindowContext`, `SkiaCVPixelBufferUtils`,
    `RNSkAppleVideo`, `RNSkAppleView` and `RNSkMetalCanvasProvider` are all
    gone; only `MetalLayerColorSpace.mm` is left. `apple/` is eleven files where
    it was fourteen, and the Ganesh ones are not among them.
  - **What replaces them is portable C++, which is better for us.** The whole
    swapchain is `cpp/rnskia/RNSkWindowSurface.{h,cpp}` over `wgpu::Surface`, and
    its own comment says the platform view "only acquires the native handle and
    forwards its lifecycle (attach, resize, detach)". `RNSkSurface` is a
    five-method abstract interface, so a host can present its own way.
  - **But `MakeWGPUSurface` is a two-branch `#ifdef`.** `__APPLE__` gives it a
    `wgpu::SurfaceSourceMetalLayer` over a `CAMetalLayer`, and the `#else` is
    `wgpu::SurfaceSourceAndroidNativeWindow`. On GTK or Win32 that compiles and
    produces a surface over a handle Dawn cannot use, so neither can call it:
    each needs its own `RNSkSurface`, built on `SurfaceSourceWindowsHWND`,
    `SurfaceSourceXlibWindow` or `SurfaceSourceWaylandSurface`, which Dawn has.
  - **And Dawn is prebuilt for Apple and Android only.** The
    `react-native-webgpu-dawn` package carries
    `libs/apple/libwebgpu_dawn.xcframework` and `libs/android/<abi>`, nothing
    else, and `RNDawnContext.h` is a hard `pod install` error without it. So GTK
    and Win32 need Dawn from source beside a Graphite-enabled Skia from source,
    which is a second `build_skia_*.sh`-shaped project each.
  - **The platform context is six virtuals rather than fifteen**, and
    `packages/basalt-skia` answers all six. Done, and on 3.x
    `makeOffscreenSurface` is Graphite's own rather than raster: a recording is
    replayed into a Graphite target, so a raster surface there is the wrong
    kind of surface rather than a slower one, and `drawAsImage` is the path
    that reaches it.
  - **`RNSkiaModule` is done**, and portable: `BasaltSkiaModule` is plain C++
    in basalt-skia rather than Objective-C++ in the AppKit package, because
    with Dawn's Metal behind `wgpu::Surface` there is nothing platform-specific
    left in either the module or the context. It and the 2.x
    `AppKitSkiaModule` answer to the same name and never exist together: the
    one is compiled only where `BASALT_SKIA` built the Ganesh half, which is
    exactly where the version guard refused 3.x.
  - **Eleven sources are the whole portable half**, plus
    `apple/MetalLayerColorSpace.mm`, which is the one Apple source 3.x still
    has that this platform needs and is React-free. `cpp/skia/modules/jsonreader/SkJSONReader.cpp`
    is compiled too, for the reason core's Skia.cmake gives: Android links a
    `libjsonreader.a` and Apple has no such archive, so leaving it out is
    `skjson::Value::toString` undefined from libskottie.
  - **What is left on macOS is `<Canvas>`**: a view owning a `CAMetalLayer`,
    handed to `RNSkWindowSurface::attach`, plus the component registered in
    `AppKitMountingManager`. The imperative API -- `Skia.Path`, image decoding,
    typefaces, `drawAsImage` -- works without it, which is most of what an app
    like kino uses outside its preview.

  Which stages, and the order is forced rather than chosen: macOS first, because
  it is the one host whose binaries exist today -- `react-native-skia-graphite-apple-macos`
  and Dawn's xcframework are both published. GTK and Win32
  wait on a Dawn build, and until then an app that wants Skia on them installs
  the 2.x line, which is still published under both names
  (`react-native-skia@release-2.x` is 2.15.1). `cmake/Skia.cmake` and the CLI
  both refuse a 3.x install by version and say that, rather than failing on a
  `MetalContext.mm` nobody deleted.
- **The version this host reports cannot express a prerelease tag.** The Android
  spec it answers types `prerelease` as an optional int, so there is nowhere to
  put `-rc.1`. Stable releases have none, so this only matters for somebody
  testing against a release candidate.
