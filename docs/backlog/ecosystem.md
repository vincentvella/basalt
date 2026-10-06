# Ecosystem

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (3):**

1. Nobody else can use this yet
2. Porting a first third-party native module end to end
3. Packaging: Arch PKGBUILD, Flatpak

- **Nobody else can use this yet**, because nothing is published, and no
  longer because installing would not work. A fresh `create-expo-app` (SDK 57,
  React Native 0.86.3) with the core packages installed from their tarballs
  runs `react-native run-linux --build` and renders the template's text, with
  nothing from this repository on the path. `.github/workflows/release.yml`
  does exactly that on every release. Three things stood in the way, all in
  `--build`, and all found by writing that job:
  - ~~**It demanded a React Native source checkout.**~~ `buildHost` refused an
    installed `react-native`, long after bootstrap had learned to fetch the
    one directory the npm package lacks and CMake to build from the rest (see
    the `ReactCxxPlatform` entry under Upstream). Its error message told people
    to pass `--react-native-path`, which is not a flag.
  - ~~**It never wired in Expo.**~~ Nothing passed
    `-DBASALT_EXPO_MODULES_CORE`, worklets or Reanimated, so an Expo app got a
    host with no Expo runtime. It now passes whichever the app has installed.
  - ~~**It named no compiler.**~~ CMake took the system default, g++ on Ubuntu,
    and React Native's `-Werror` stopped it in ReactCommon. It names clang now,
    and on Windows clang-cl, a build type and vcpkg's toolchain file.

  On Windows the same app runs too, from `npm run windows -- --build` in a
  plain PowerShell: WSL's `bash.exe` first on PATH, no cmake, no vcvars.
  Getting there took `--build` finding Git Bash and loading the MSVC
  environment itself, and two fixes for expo-modules-core's C++, which had
  never been compiled by anything but clang and GCC: `JSI/ObjectDeallocator.h`
  says `#import`, which clang-cl reads as a COM type-library import, so on
  Windows the build compiles a copy with it spelled `#include`; and
  `TypedArray.cpp` throws `std::runtime_error` without `<stdexcept>`, which
  Microsoft's standard library does not pull in for it. Both are worth
  reporting to Expo.

  What is left before publishing is ordinary: versions, an npm account, the
  publish step, and an install guide. Plus one thing that is not ordinary and
  should happen first: deciding which capabilities ship separately, because
  moving one after the first publish is a breaking change rather than a commit.
  See `openspec/changes/split-optional-capabilities-into-packages`, whose first
  move (notifications) is done. And a first `--build` that compiles Hermes
  and React Native's C++ from source, which is the part a user will notice.
- ~~**Adding a desktop to an Expo app is manual.**~~ Done:
  `npx basalt-core init` adds this package and the two dev dependencies
  an app needs: `@react-native/metro-config`, which React Native's `start`
  requires whatever the app's Metro config says, and
  `@react-native-community/cli`, which provides `run-windows`: wraps the
  Metro config, and adds a script per desktop.

  Idempotent, and it refuses rather than half-configuring: run twice it reports
  what is already right and writes nothing, and run somewhere it cannot
  identify as an app it says what it expected and leaves the directory alone. A
  `metro.config.ts` it cannot safely edit is reported rather than overwritten.

  What is left is the verification the change asked for and this did not do:
  running it against a fresh `create-expo-app`, the way `release.yml`'s install
  job installs the packed packages, and building the result. The unit tests
  cover what it writes; nothing yet covers that what it writes is sufficient.
- **Porting a first third-party native module end to end**, to learn what the
  porting story actually costs. This is the largest unknown in the project: the
  TurboModule seam is proven, by `src/LinuxPlatformConstants.cpp`, but no
  third-party module has been through it, and there is no codegen configuration
  for one. Until a module has been ported, the cost of porting any module is a
  guess.
- Packaging: Arch PKGBUILD, Flatpak.

- ~~**The package ships no types, and an Expo app is TypeScript by default.**~~
  Done: the package is TypeScript and publishes its declarations, so
  `import {useWindow} from 'basalt-core'` in a `create-expo-app` project
  is typed. `docs/DECISIONS.md` records why that rather than JSDoc, and
  `docs/ARCHITECTURE.md` what the build step costs.

  The two files that stay JavaScript are the CLI manifests, which React Native's
  CLI loads as plain CommonJS by path convention; they are checked with
  `@ts-check` rather than compiled.

## Navigation: done, except for the stack itself

Implemented 2026-10-02 in `core/SafeAreaComponent.{h,cpp}` and
`core/ScreensComponent.{h,cpp}`, registered on all three hosts. Verified by
running react-navigation and expo-router on the AppKit host and reading the
mounted tree: both render and both navigate with no change to the application.

What was wrong, and is not any more:

- **`RNCSafeAreaProvider`** was the whole blank-window failure.
  `SafeAreaProvider` renders null until it has insets, and the only thing that
  delivers them is this component's `onInsetsChange`. The event is emitted from
  the shadow node's `layout()` rather than from a mounting peer, because the
  insets are zero on every desktop and the frame is the node's own layout, so
  there was nothing left for a host to contribute.
- **`RNCSafeAreaContext`** answers `initialWindowMetrics`, fed by each host
  through `basalt::setInitialWindowFrame` from its own `kInitialWidth`.
- **`RNSModule`** exists and is empty, which is exactly what its TypeScript spec
  is: `interface Spec extends TurboModule {}`. The library only asks whether it
  is there.
- **`RNSScreen` and the container components** are registered, and a screen
  whose `activityState` is 0 is given `display: none`, which takes it out of
  layout on every host at once.

### The stack, done 2026-10-02

`RNSScreenStackShadowNode::layout` gives every screen below the topmost opaque
one `DisplayType::None`, which all three mounting managers already turn into a
hidden widget and which therefore takes it out of accessibility on each of them.
A transparent modal covers nothing, so the search for the topmost opaque screen
walks past one; `stackPresentation` is parsed for that and nothing else.

Done in the shadow node rather than in three mounting managers because the only
inputs are the child list and one prop, and RN's own
`YogaLayoutableShadowNode::layout` already writes children's metrics through
`ensureUnsealed()`, so there was a sanctioned place to do it. Verified on both
the AppKit and GTK hosts from the same code, by `e2e/screens.tsx` and the
"a screen stack shows its top screen" scenario.

The route that looks right and is not: `activityState`. Basalt hides a screen
whose `activityState` is 0, which is correct and, in a native stack, never
happens. react-native-screens throws `activityState cannot be decreased in
NativeStack`, and a `ScreenContext` spy shows every screen in a pushed stack
reporting 2, the covered one included.

What this does not do is skip the layout. Yoga has measured the covered screens
before `layout()` can see which is on top, and getting in front of that would
need a screen to know its own position in the stack. The cost is measuring a
subtree that is not shown.

### The header, done 2026-10-03

A bar when it has content, nothing when it does not, and the screen's body
moved down below it rather than left behind it.

Two things here were learned the hard way and are worth keeping.

**A props constructor cannot style this node.** Setting `yogaStyle` in
`RNSScreenStackHeaderConfigProps` has no effect: the yoga node holds its own
copy of the style, and the props the node ends up with are not always the ones
the constructor produced. Measured, after a props-side height left the bar
hugging its text and a probe showed the node's own props reporting no height at
all. `adopt()` is where it works, through `setSize` and `setPadding`, which
write to the yoga node and mark it dirty. `ModalHostViewComponentDescriptor`
upstream sizes itself the same way.

**Padding rather than a height**, because `adopt()` reaches only `setSize`,
`setPadding` and `setPositionType`. A height set through `setSize` leaves the
title against the top edge, since `alignItems` lives in the props and the props
are the half that does not arrive. Sizing the bar as its content plus a margin
lands within a few points of the 56 every toolkit uses and centres a one-line
title without having to know how tall it is.

What is still not drawn is a plain string `title`. react-navigation sends a
left-aligned string to the native navigation bar as a prop and renders it as a
view only when it is centred or supplied as a component, so there is nothing to
lay out. Drawing it would mean a text-drawing header widget in each of the
three mounting managers, which is the toolkit header this entry used to call
for. An empty header now takes itself out of the layout, so the default reads
as no header rather than as a broken one.

### Two things an app still has to do

- **Nothing, on Windows. On macOS and Linux, wait for upstream.**
  react-native-screens decides whether to use its native components at all with

  ```ts
  export const isNativePlatformSupported =
    Platform.OS === 'ios' || Platform.OS === 'android' || Platform.OS === 'windows';
  ```

  a const in its own `core.ts`, and `Screen` renders natively only when it is
  true. Basalt's Windows host reports `windows`, so it is on that list and gets
  all of the above today, which is a dividend of having kept
  react-native-windows' platform name. On the other two the library renders
  plain views and `enableScreens()` does not change it, because that function
  sets a different flag.

  Proposed upstream as
  [software-mansion/react-native-screens#4779](https://github.com/software-mansion/react-native-screens/pull/4779),
  as an opt-in rather than a longer list. Two reasons it is not a longer list,
  both found by trying it:

  - That const is not "platforms where this could work", it is "platforms this
    library ships native code for". Their podspec covers ios, tvos and
    visionos, and there is a `windows/` directory. `macos` is absent because
    react-native-macos reports `macos` and the library has no macOS code, so
    adding the name would claim support that does not exist.
  - Relaxing the component gate to `enabled` alone does not work either, which
    is not visible from reading it: `ScreenStackItem` passes `enabled` as a bare
    prop to every `Screen` it renders, so the platform term is the only thing
    keeping an unsupported platform off the native components. Measured, not
    reasoned about: the first patch hid the covered screen even with no
    `enableScreens()` call, which is how it was caught.

  Forking `core.ts` through a Metro override would work today and is not worth
  it: metro-config.ts argues that the library fallback "cannot be a list of
  names", and owning a third-party module's gate for one library is the worse
  problem.
- **Match expo-router to its SDK's React Native.** SDK 57 targets 0.86.3, and
  against 0.87 the bundle fails inside Expo's Metro config looking for
  `react-native/rn-get-polyfills`, which 0.87 does not ship. Not ours to fix.
