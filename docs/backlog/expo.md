# Expo, beyond the template

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (12):**

1. expo-image's decorative props
2. More Expo views
3. A URL delivered to a *running* app
4. The clipboard's image and URL types
5. A frame source the animation systems can share
6. `core/ReanimatedCompat.h` has never been compiled
7. Reanimated's layout animations and shared element transitions
8. `synchronouslyUpdateUIProps`
9. The gestures a cursor cannot make
10. The rest of RNGH's relation graph
11. A visible error when the bundle throws
12. A local module's Apple half is Swift, and the desktop half is written again

Measured in phase 34 against a real dependency set, on both desktops.

- **expo-image's decorative props**: placeholder, transition, blurhash, cache
  policy. Left out of the view config in phase 36, so they are dropped in
  JavaScript and an app gets an image without them.
- **More Expo views.** The seam exists now (phase 36), so expo-linear-gradient,
  expo-blur and the rest are each a props class, a descriptor and a mounting
  peer rather than a new mechanism.
- **A URL delivered to a *running* app.** `Linking.getInitialURL()` works on all
  three now: a desktop hands a URL over on the command line, so each host
  records whichever argument carried a scheme. What is still missing is the
  second delivery, to an application that is already open, and that is where the
  three desktops diverge: a GApplication with `G_APPLICATION_HANDLES_OPEN` on
  Linux, an Apple Event handler and a registered scheme on macOS, a named pipe
  and a shell association on Windows. `ExpoLinking.getLinkingURL` is the same
  question and would come with it.
- **The clipboard's image and URL types**, which are separate pasteboard types
  on each platform. Absent rather than stubbed, so expo-clipboard reports them
  as unavailable by name.
- **A frame source the animation systems can share.** Worklets and Reanimated
  both run on a sixteen-millisecond timer, because each platform's display link
  belongs to React Native's `AnimationChoreographer` and pauses whenever React
  Native has no animation of its own.
- **`core/ReanimatedCompat.h` has never been compiled.** It exists for a
  platform that is neither Android nor Apple; the Linux host here is built on
  macOS. The first real Linux build is its first test.
- **Reanimated's layout animations and shared element transitions** compile and
  have never been run.
- **`synchronouslyUpdateUIProps`**, Reanimated's direct-to-view path, is a
  no-op: the mounting managers accept mutations only from a Fabric transaction.
- **The gestures a cursor cannot make**: pinch, rotation and force touch begin
  and fail, because there is no second finger and no pressure. Two-finger
  trackpad gestures exist on both desktops and are not wired to anything.
- **The rest of RNGH's relation graph**: `blocksHandlers`, and a `waitFor` that
  resolves across detectors rather than within one gesture.
- **A visible error when the bundle throws.** Today the window stays empty and
  the only evidence is a line in the host's log. Every failure above, and every
  future one, is invisible to whoever is running the app.

- **A local module's Apple half is Swift, and the desktop half is written
  again.** This platform already meets Expo's convention halfway and the
  remaining distance is a second implementation per module, which an app pays
  today rather than in principle.

  What exists: `cli/desktop.ts` looks in `<projectRoot>/modules/*` -- the exact
  directory Expo's autolinking looks in -- and takes any local module with a
  `native/CMakeLists.txt`. Presence is the declaration, because
  `create-expo-module --local` writes a config, an index and a platform
  directory and no package.json, so there is nothing to declare it in.

  What does not: `expo-module.config.json`'s `apple.modules` names Swift
  classes and nothing here reads that key, because nothing here compiles
  Swift. Measured in kino on 2026-10-12:

  | its module | Apple half | desktop half |
  | --- | --- | --- |
  | `kino-audio` | `KinoAudioModule.swift` + a podspec | `ExpoKinoAudioModule.cpp`, plus appkit, gtk and win32 |
  | `kino-process` | `KinoProcessModule.swift` | none; its index.ts falls back to basalt-subprocess |

  So one module is written twice and the other is routed around in JavaScript.

  **Compiling Swift is the obvious answer and probably the wrong one.** Swift
  runs on Linux and Windows now, so the language is not the obstacle; the
  `Module { Name(); Function() }` DSL is, because it lives in ExpoModulesCore's
  Swift half, which is built for iOS. Porting that is an unmeasured dependency
  project whose payoff would be modules that work on macOS and neither other
  host -- the per-host asymmetry that cost four bugs in one evening when
  basalt-skia had it. Worth revisiting only with a measurement of what
  ExpoModulesCore actually needs.

  **The cheaper increment is to narrow the distance, in two places.** Read
  `expo-module.config.json` so a module's *name* is declared once rather than
  restated in the C++, and thin `core/ExpoModules.h` toward the shape of the
  Swift DSL -- kino's `ExpoKinoAudioModule.cpp` is already "the JavaScript
  boundary and nothing else", so the gap there is ergonomics rather than
  capability. And say something at build time when a local module has an
  `apple/` or `ios/` half and no `native/` one: the failure today is
  `Cannot find native module 'KinoProcess'` at runtime, which names the module
  and not the reason.
