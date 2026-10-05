# Compatibility

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (7):**

1. React Native 0.82 and older cannot reach a native module
2. React Native 0.83 through 0.86 are refused rather than untested-but-allowed
3. Supporting React Native 0.81, which is what kino pins
4. CI checks one React Native per run
5. Expo starts but no Expo module works
6. No @shopify/react-native-skia, which stops one real app dead
7. The version this host reports cannot express a prerelease tag

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
  while bundling `e2e/skia.js`. That was wrong and the reasoning was bad: the two
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
  the first time it ran. React Native's `main` hand-declares `Blob`, `File`,
  `FileReader`, `requestAnimationFrame` and `requestIdleCallback` in
  `src/types/globals.d.ts`; 0.87.1 has no such file, so `@types/node`'s `buffer`
  Blob wins and the other four are undeclared. Anything that merges into one of
  those ambient names is therefore version-dependent. e2e/runtime.ts is the
  answer for the demo apps: it names the surface the *runtime* installs and
  reaches it through one cast of `globalThis`, so no ambient declaration has to
  win. Checked both ways by moving `globals.d.ts` aside.
- **Expo starts but no Expo module works.** `globalThis.expo` is installed now, from the
  app's own expo-modules-core, so an Expo app starts and renders. What does not
  exist is any Expo *module*: `ExpoAsset` and `ExponentConstants` are stubs that
  exist only so Expo's start-up survives, and `expo-font`, `expo-image` and
  every config plugin's native half are each their own port.
- **No `@shopify/react-native-skia`**, which is the thing that stops kino dead,
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

  GTK and Win32 are a different proposition: no prebuilt Skia, and a GL, Vulkan
  or D3D window context rather than Metal.
- **The version this host reports cannot express a prerelease tag.** The Android
  spec it answers types `prerelease` as an optional int, so there is nowhere to
  put `-rc.1`. Stable releases have none, so this only matters for somebody
  testing against a release candidate.
