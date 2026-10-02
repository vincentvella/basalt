# basalt-core

React Native on the desktop (Linux, macOS and Windows) built on React
Native's *own* C++ core rather than on a fork of it.

Basalt is the bedrock everything else sits on, and it forms columns: one shared
mass, separate columns standing on it. That is the architecture. React Native's
C++ platform, Hermes, Yoga, Fabric's mutation walk and the JavaScript platform
layer are shared; a GTK4 widget layer, an AppKit view layer and a Win32 one
stand on them separately. Nothing is forked, which is why this can track the
current Expo instead of trailing a rebase, and why it is on React Native
0.87 while react-native-windows is on 0.84 and react-native-macos on 0.81.

Today all three desktops mount the components an ordinary app is built from:
`<View>`, `<Text>`, `<Image>`, `<ScrollView>`, `<TextInput>`, `<Switch>`,
`<ActivityIndicator>`, `<Modal>` and `<RefreshControl>`, and answer a press, a
wheel, a hover, a Tab and a keystroke with the events React Native says they
should. `Alert`, `Share`, `Linking`, `Animated` with the native driver,
`require()`d assets, React Native's own LogBox inspector and its developer menu (Ctrl+D, Cmd+D on macOS) work on all three; `run-macos` builds a real `.app`
so notifications and the Dock icon work, and `run-linux` writes a `.desktop`
entry; and `useDialog()`, `useWindow()`, `<Menu>` and `<Window>` cover the native file
dialogs, the window's own geometry, the application menu, context menus and
opening a second window, none of which React Native has an API for because a phone has none of
them: a window can also refuse to close and ask first, which is where an app
with unsaved work puts the question, and say how big it may be;
so do Reanimated, gesture-handler, expo-image and a proxy for
`expo-notifications`. The demo app written for GTK runs on each of them
unchanged, and `react-native run-linux`, `run-macos` and `run-windows` are one
command with three names.

Linux is the most finished: it runs real Expo apps, with fonts and assets, and
CI builds and tests it in full. macOS and Linux produce a byte-identical view
tree for every app in `js/`; `scripts/compare_all.sh` is what says so, and
`scripts/compare_hosts.sh` is the single-app version. Windows and Linux agree on
all of them too, run side by side on one Windows machine with the GTK host in
WSL, see `docs/TESTING.md`.

Windows is where this project first asserts on pixels rather than on a tree,
because Direct2D renders offscreen with no window and neither other toolkit
does.

This was called `react-native-linux` until it stopped being about Linux.

## Why this is tractable

React Native already ships a generic C++ platform that is neither iOS nor
Android: `packages/react-native/ReactCxxPlatform/`, a sibling of `ReactAndroid`
and `ReactApple`. It provides `ReactHost` (instance lifecycle, surfaces,
reload), `SchedulerDelegateImpl`, and implementations of http, io, logging,
threading, profiling, devsupport, coremodules and TurboModule hosting.

`private/react-native-fantom/tester/` is a CMake-built C++ host that already
runs on Linux: its `CMakeLists.txt` has an explicit `if(UNIX AND NOT APPLE)`
branch, and `getHostPlatform.js` maps `process.platform === 'linux'` to a
supported host. So Hermes, Yoga and Fabric already build and run here.

`ReactHost`'s constructor takes a `std::shared_ptr<IMountingManager>`. That
interface has exactly two pure-virtual methods, `executeMount` and
`dispatchCommand`. Implementing it against GTK4 is the core of this project.

## Layout

Four npm packages, and the split is the architecture rather than tidiness.

    packages/basalt-core          the shared half
    packages/basalt-gtk      Linux, over GTK4
    packages/basalt-appkit   macOS, over AppKit
    packages/basalt-win32    Windows, over Win32 and Direct2D

`basalt-core` is everything that is not about a toolkit: the JavaScript
platform layer, the bundler, React Native's C++ core and Hermes, the Expo
runtime, the core TurboModules, and Fabric's mutation walk. It builds and
*links* with no toolkit at all, which is how that claim is kept honest rather
than aspirational; `basalt_core_probe` is a program that links it and nothing
else, built on every build.

A platform package is a view layer, a mounting manager and a host, and nothing
more. Each adds the shared one to its CMake build and skips itself where it
cannot compile, so a Mac with no GTK builds the AppKit half alone, a Linux box
never asks for an Objective-C++ compiler, and a Windows box with neither builds
the Win32 half.

Every host produces a byte-identical view tree from the same script;
`scripts/compare_hosts.sh` is what says so, for whichever of them are built.

Inside the shared package, under `native/`:

    core/MountingWalk.h         Fabric's mutation semantics, once. Every platform
                                supplies seven operations; the Create/Insert/
                                Remove/Update/Delete rules are here and shared.
    core/ComponentRegistry.h    Documents which components a platform claims --
                                a seam, defined per platform.
    core/ExpoRuntime.*          globalThis.expo, and the modules on it.
    core/PlatformConstantsModule.*  Platform.OS and friends.
    core/SourceCodeModule.*     Where the bundle came from. Assets need it.
    core/StatusBarModule.*      StatusBar, reporting a height of zero.
    core/HttpClient.cpp         The IHttpClient seam, backed by libcurl.
    core/FontRegistry.h         One of the three seams a new desktop must fill.
    core/ImageBytes.*           The bytes behind an <Image>: file, data, http.
                                Shared, because a URI means the same everywhere.
    core/portability_probe.cpp  Links core alone, so the claim is a build failure
                                rather than a paragraph.
    tests/TestHarness.*         The test runner. No toolkit; each platform
                                package brings its own `main`.
    bootstrap.sh                Vendors folly, Hermes and RN's codegen output.
    cmake/                      React Native's C++ core, third party, Expo.
    src/overrides/              The JavaScript React Native cannot supply for a
                                platform it has never heard of.
    metro-config.js             withDesktopPlatforms: linux, macos, windows.
    cli/                        The bundler, Metro, and the desktop run command
                                the three platform packages name.

`basalt-gtk`, under `native/`:

    gtk/RnView.h/.cpp           GTK4 widget layer. No RN dependency.
    gtk/GtkMountingManager.*    The GTK half of IMountingManager: making a widget,
                                parenting it, turning a ShadowView into widget
                                state. The walk is in the shared half.
    gtk/ComponentRegistryGtk.cpp  What GTK claims: View, Paragraph, Text, RawText,
                                Image, ScrollView, TextInput, and the controls
                                -- ActivityIndicatorView, Switch, ModalHostView,
                                PullToRefreshView.
    gtk/GtkAnimationChoreographer.*  AnimationChoreographer on GTK's frame clock.
    gtk/GtkTouchDispatcher.*    GTK input -> RN touch events. Hit test included.
    gtk/GtkImageLoader.*        <Image> pixels. RN's cxx platform provides none.
    gtk/GtkScrollView.*         <ScrollView>: offset, clipping, onScroll, state.
    gtk/GtkTextInput.*          <TextInput>, over a real GtkText.
    gtk/GtkRunLoopObserver.*    Drives the event beat. Without it, no event an
                                emitter produces ever reaches JavaScript.
    gtk/PangoTextLayout.*       AttributedString -> PangoLayout. Shared by both
                                measurement and painting, so they agree.
    gtk/PangoTextLayoutManager.cpp  Replaces RN's stub TextLayoutManager.
    gtk/FontRegistryFontconfig.cpp  The font seam, on fontconfig.
    gtk/main_gtk.cpp            The host: ReactHost, Hermes, one live surface.
    gtk/demo_layout_gtk.cpp     Hand-written frames; no RN needed.
    gtk/mount_harness_gtk.cpp   Hand-built mutations; no JS runtime.
    cli/                        run-linux. Four strings; the command itself is
                                basalt-core/cli/desktop.js.

`basalt-appkit`, under `native/`:

    appkit/RnAppKitView.h/.mm   AppKit view layer. Flipped, layer-backed.
    appkit/AppKitMountingManager.*  The AppKit half of IMountingManager: making a
                                view, parenting it, turning a ShadowView into
                                view state. The walk is in the shared half.
    appkit/ComponentRegistryAppKit.mm  What macOS claims: View, Paragraph, Text,
                                RawText, ScrollView, Image, TextInput. See
                                core/ComponentRegistry.h.
    appkit/AppKitTouchDispatcher.*  Mouse -> RN touch events. Hit testing is in
                                the view layer, so it tests without RN.
    appkit/AppKitScrollView.*   <ScrollView>: offset, clipping, onScroll, state
                                and commands. bounds.origin, not NSScrollView.
    appkit/AppKitImageLoader.*  <Image> pixels, decoded off the main thread.
    appkit/AppKitTextInput.*    <TextInput>, over a real NSTextField.
    appkit/AppKitRunLoopObserver.*  The event beat, on a CFRunLoopObserver.
    appkit/AppKitAnimationChoreographer.*  Animation frames, on a CADisplayLink.
    appkit/FontRegistryCoreText.mm  macOS's half of the font seam.
    appkit/RnTextLayout.*       A laid-out paragraph. Core Text only, no RN.
    appkit/CoreTextLayout.*     AttributedString -> Core Text. Shared by
                                measurement and painting, so they agree.
    appkit/CoreTextLayoutManager.mm  Replaces RN's stub TextLayoutManager.
    appkit/AppKitSnapshot.*     Renders a view tree to a PNG with no window.
    appkit/main_appkit.mm       The host: ReactHost, Hermes, one surface in an
                                NSWindow.
    appkit/demo_layout_appkit.mm    The same boxes as GTK's, on AppKit.
    appkit/mount_harness_appkit.mm  The same two transactions as GTK's.
    cli/                        run-macos, the same four strings.

`basalt-win32`, under `native/`:

    win32/RnWin32View.h/.cpp    Win32 view layer, painted with Direct2D. Not one
                                HWND per view; see docs/DECISIONS.md.
    win32/Win32Snapshot.*       One offscreen render, two exits: a PNG, and the
                                pixels tests/test_win32_paint.cpp asserts on.
                                No window, no device, no display.
    win32/RnWin32TextLayout.*   A laid-out paragraph, over DirectWrite. No RN.
    win32/RnWin32Image.*        <Image> pixels, over WIC. Device-independent.
    win32/RnWin32Accessible.*   UI Automation. A provider answers questions,
                                where GTK and AppKit take properties.
    win32/Win32MountingManager.*  The Windows half of IMountingManager. The walk
                                is in the shared half.
    win32/ComponentRegistryWin32.cpp  What Windows claims: View, Paragraph,
                                Image. See core/ComponentRegistry.h.
    win32/DirectWriteLayout.*   AttributedString -> RnWin32TextLayout. Shared by
                                measurement and painting, so they agree.
    win32/DirectWriteLayoutManager.cpp  Replaces RN's stub TextLayoutManager.
    win32/FontRegistryDirectWrite.cpp   The font seam.
    win32/ColorSchemeWin32.cpp  Light or dark, from the registry.
    win32/PlatformServicesWin32.cpp  Clipboard, URLs, alerts -- and
                                postToUiThread, on a message-only window.
    win32/demo_layout_win32.cpp The same six boxes as GTK's and AppKit's.
    win32/mount_harness_win32.cpp  The same two transactions as GTK's.
    win32/Win32ImageLoader.*    Fetch and decode off the UI thread.
    win32/Win32TouchDispatcher.*  Mouse messages -> React Native touches, and
                                the hit chain RNGH is offered. Capture is the
                                host's, in main_win32.cpp.
    win32/Win32ScrollView.*     <ScrollView>: the offset, the state write-back,
                                the commands, and the wheel's routing -- which
                                is a parent walk here, since there is no
                                responder chain to hand it to.
    win32/Win32TextInput.*      <TextInput>, over a real EDIT control. The only
                                peer on this platform that is a *window*, so it
                                is also the only one the host has to place,
                                forward messages to and colour.
    win32/Win32RunLoopObserver.*  The event beat. On Windows the message loop
                                itself, because there is no hook to install.
    win32/Win32AnimationChoreographer.*  Animation frames, on a timer.
    win32/main_win32.cpp        The host: ReactHost, Hermes, one surface in an
                                HWND.
    cli/                        run-windows, the same four strings.

And at the repository root:

    CMakeLists.txt              The development build: adds all three packages.
    js/index.js                 The demo app. Ordinary React Native.
    js/views.js                 A React app made only of <View>. What
                                compare_hosts.sh runs by default.
    js/text.js                  A React app that is mostly <Text>.
    js/press.js                 A <Pressable> that counts presses as boxes.
    js/scroll.js                A <ScrollView> that scrolls itself by command.
    js/image.js                 Four resize modes, a data: URI, a broken source.
    js/a11y.js                  Roles, labels, hints, states and hiding.
    js/input.js                 A controlled field that upper-cases what it is
                                given, which is how the loop is checked.
    js/demo.js                  Drives Fabric's JSI binding by hand. No React.
    js/metro.config.js          Resolves react/react-native out of the checkout.
    examples/demo/              A small app that consumes it like a stranger.
    scripts/bundle.sh           Builds a bundle. scripts/metro.js serves one.
    scripts/compare_hosts.sh    Runs one app through every host that is built
                                and diffs the trees. Needs two toolkits on one
                                machine, so not CI.
    scripts/compare_all.sh      Every app in js/, through all of them. The
                                summary.
    scripts/integration_test.py End to end, against whichever host is built.
    scripts/test_cli.js         run-linux, run-macos and run-windows, which are
                                one function with three names.

`RnView` is a `GtkWidget` subclass paired with `RnLayout`, a `GtkLayoutManager`
that performs no layout: Yoga has already resolved absolute frames by the time
a mutation arrives, so `RnLayout::allocate` places each child at the rect the
shadow tree assigned it, and `measure` reports zero so GTK never second-guesses
Yoga.

## Mutation semantics

Read off `StubViewTree::mutate`, RN's own reference walk:

| Mutation | `parentTag` | Meaning |
|---|---|---|
| `Create` | `-1` | Allocate a widget, register by tag. Not attached yet. |
| `Delete` | `-1` | Unregister and drop the last reference. |
| `Insert` | parent | Attach an existing child at `index`. |
| `Remove` | parent | Detach, but do **not** destroy; a `Delete` may follow, or a re-`Insert`. |
| `Update` | parent | New props / layout metrics for an existing tag. |

Two consequences the implementation depends on:

- Create/Delete are registry operations; Insert/Remove are tree operations. A
  view can sit in the registry with no parent between a Remove and its Delete,
  so the registry holds a strong reference (`g_object_ref_sink` on Create).
- `mutation.mutatedViewIsVirtual()` marks views that exist only in the shadow
  tree to keep an `EventEmitter` alive. They have no widget; skip them on
  Insert and Remove.

Fabric emits no `Create` for a surface root; the root shadow node is the base
of every diff. The host calls `createSurfaceRoot(surfaceId)` before
`ReactHost::startSurface`. In Fabric a `SurfaceId` *is* the root node's tag,
which lets the root live in the same registry as every other view.

## Threading

`executeMount` arrives on the **JS thread**, from the event loop's "update the
rendering" step (`RuntimeScheduler_Modern::updateRendering`, called with the
`jsi::Runtime` live). GTK widgets are main-thread-only, so `executeMount` only
queues the transaction onto a GLib idle source; `applyTransaction` does the
widget work on the main thread and asserts it got there. iOS and Android
marshal at the same point.

Queuing is unconditional, even when the caller is already the main thread:
equal-priority GLib idle sources run in insertion order, which is what
preserves mutation ordering.

## Build

From inside this repo. The `../` puts React Native alongside it, which is where
`bootstrap.sh` looks by default, or pass the path to a checkout you already
have, absolute or relative:

    git clone --depth 1 https://github.com/react/react-native ../react-native
    scripts/bootstrap.sh ../react-native

    cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DRN_DIR=../react-native/packages/react-native
    nice -n 10 cmake --build build -j 12

`scripts/bootstrap.sh` fetches the vendored third-party sources, builds Hermes,
and runs React Native's codegen; everything below that is not in the repo. It
is idempotent, and `--force` redoes it. See `docs/HANDOFF.md` for setting up on
a different machine.

The manual equivalents of each bootstrap step are documented below.

Produces `librn_view.a`, `librn_mounting.a` and the `demo_layout_gtk` executable.
Omit `-DRN_DIR` to build only the widget layer and the demo.

### On Windows

Two builds, and the smaller one is worth knowing about because it needs almost
nothing.

**The view layer alone** needs no React Native, no bootstrap and no third-party
anything: Direct2D, DirectWrite, WIC and UI Automation all ship with the
operating system. Visual Studio Build Tools with the C++ workload, CMake and
Ninja, from a shell that has run `vcvars64.bat`:

    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
      -DCMAKE_CXX_COMPILER=clang-cl
    cmake --build build
    build\basalt_win32_tests.exe

`clang-cl` because `docs/DECISIONS.md` prefers clang where there is a choice; it
is a component of the same Build Tools install, and dropping the flag builds
that half with `cl` instead.

**The whole thing** additionally needs what Linux gets from apt, which on
Windows means vcpkg, and Node 24, React Native 0.87 refuses anything below
`^22.13`. Run `scripts/bootstrap.sh` from Git Bash; it fetches folly and Hermes,
patches Hermes, builds it, runs codegen, and prints the configure line. It
refuses early and says what to install if vcpkg is missing its packages.

    git clone https://github.com/microsoft/vcpkg ~/Tools/vcpkg
    ~/Tools/vcpkg/bootstrap-vcpkg.bat
    vcpkg install --triplet x64-windows glog fmt double-conversion \
        boost-regex boost-beast boost-asio boost-thread openssl curl

    scripts/bootstrap.sh ../react-native

Unlike the view layer, the core build **requires** clang-cl rather than `cl`:
React Native's own CMake sets clang-style flags that `cl` does not understand,
and Static Hermes uses `__builtin_expect`, which it does not have. See
`docs/PORTING.md`, which also records the three fixes Hermes needs on
Windows and why the prebuilt Hermes react-native-windows uses cannot be
substituted.

To run an app that uses Expo modules, Reanimated or worklets, point the build at
that app's copies of them. Each is optional and each is compiled from the app's
own `node_modules`, so the versions can never drift from the JavaScript:

    -DBASALT_EXPO_MODULES_CORE=<app>/node_modules/expo-modules-core
    -DBASALT_WORKLETS=<app>/node_modules/react-native-worklets
    -DBASALT_REANIMATED=<app>/node_modules/react-native-reanimated

`-DBASALT_REANIMATED` needs `-DBASALT_WORKLETS`, because Reanimated 4 is built
on worklets. Without them a plain React Native app is unaffected and an app that
imports one fails at that import, which is what it did before.

`react-native run-linux --build` (and `run-macos` and `run-windows`) passes
whichever of the three the app has installed, and builds against the app's
installed `react-native` rather than asking for a checkout, so these flags are
only for building the host by hand.

### Dependencies

System packages.

    Arch    gtk4 pango google-glog boost gflags fmt double-conversion
            openssl curl cmake ninja clang
    Debian  libgtk-4-dev libpango1.0-dev libglib2.0-dev libgoogle-glog-dev
    Ubuntu  libboost-dev libboost-regex-dev libfmt-dev libgflags-dev
            libdouble-conversion-dev libssl-dev libcurl4-openssl-dev
            build-essential clang cmake ninja-build pkg-config

Ubuntu 24.04 ships Node 18, which React Native rejects; install 24 from
NodeSource. See `docs/TESTING.md` for the whole recipe, including running the
suites on Linux.

`boost` is mostly headers, but `boost_regex` is linked: folly's URI parser needs
it, and React Native's websocket client needs folly's URI parser.

Vendored, pinned to the versions RN builds against
(`packages/react-native/gradle/libs.versions.toml`):

    mkdir -p third_party && cd third_party
    curl -L https://github.com/facebook/folly/archive/v2024.11.18.00.tar.gz | tar xz
    mv folly-2024.11.18.00 folly
    curl -L https://github.com/fastfloat/fast_float/archive/v8.0.0.tar.gz | tar xz
    mv fast_float-8.0.0 fast_float
    mkdir -p nlohmann_json/include/nlohmann && curl -L -o nlohmann_json/include/nlohmann/json.hpp \
      https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp

Only headers are used from these, so none needs building.

### Hermes

Pinned by `ReactAndroid/hermes-engine/build.gradle.kts` (currently
`250829098.0.0-stable`). Built with RN's own host flags:

    curl -L https://github.com/facebook/hermes/tarball/250829098.0.0-stable | tar xz
    # -> third_party/hermes

    cmake -G Ninja -S third_party/hermes -B third_party/hermes-build \
      -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
      -DJSI_DIR=$RN_DIR/ReactCommon/jsi -DCMAKE_BUILD_TYPE=Release \
      -DHERMES_ENABLE_DEBUGGER=True -DHERMESVM_HEAP_HV_MODE=HEAP_HV_PREFER32
    cmake --build third_party/hermes-build --target hermesvm

Hermes' public headers must be on the *global* include path: RN's own
`hermes/executor` and `hermes/inspector-modern` targets include
`<hermes/hermes.h>` without declaring a dependency that carries it. Fantom does
the same with a global `include_directories()`.

### Codegen

24 targets need `react_codegen_rncore`; including ReactCxxPlatform's
`react/runtime`, where `ReactHost` lives, so codegen is mandatory for any host
that runs JS. It is a Node tool, and RN's monorepo must be installed first.

**Node version matters.** RN requires `^22.13.0 || ^24.3.0 || >= 26.0.0`.
Node 25.x is explicitly excluded and `yarn install` fails on an engine check in
a preinstall hook. Use e.g. `mise exec node@24 -- yarn install`.

    # in the React Native checkout
    mise exec node@24 -- yarn install
    mise exec node@24 -- node packages/react-native/scripts/generate-codegen-artifacts.js \
      -p packages/react-native -t android -o /tmp/rncodegen -s library -f

Copy `/tmp/rncodegen/android/app/build/generated/source/codegen/jni/react` to
`third_party/codegen/react`. The generator also emits a CMakeLists that links
Android-only targets (`fbjni`, `turbomodulejsijni`); `third_party/codegen/
CMakeLists.txt` in this repo replaces it, exactly as Fantom's
`tester/codegen/CMakeLists.txt` does. Like Fantom, it compiles only the Fabric
component specs: the top-level `FBReactNativeSpec-generated.cpp` is the JNI
TurboModule binding and does not build off Android.

### Version drift

RN pins glog `0.3.5`, boost `1_83_0`, fmt `12.1.0`. Arch ships glog `0.7.1`,
boost `1.92.0`. One real incompatibility surfaced: glog >= 0.6 added a guard
requiring `GLOG_USE_GLOG_EXPORT` to be defined when the header is not consumed
through its CMake target, which RN's pinned 0.3.5 predates. Without it every
glog header fails with *"was not included correctly"*. The define is set in
`CMakeLists.txt`. Boost 1.92 caused no problems.

RN headers also carry Xcode's `#pragma mark`, so GCC needs
`-Wno-unknown-pragmas`.

## Bundling

    basalt-bundle --bundle-output build/main.jsbundle.js

One bundle, its assets beside it, which is where React Native looks for them.
`react-native run-linux --mode release` calls the same code. An Expo app needs
this command, because `expo` does not know this platform and Metro's own `build`
emits no assets.

## A stock Expo app runs, on both

`npx create-expo-app --template blank`, unmodified, renders on Linux **and on
macOS**, and produces the same view tree on each. Expo 57, React Native 0.86.3,
both stock from npm, on a platform that forks neither.

One command makes the change:

```
npx basalt-core init
```

It adds this package, a host package per desktop, and the two dev dependencies
an app needs to bundle (`@react-native/metro-config` and
`@react-native-community/cli`); wraps the app's Metro config; and adds a script
per desktop. Running it again reports what is already right and writes nothing;
run somewhere that is not an app, it says what it expected to find and leaves
the directory alone.

The whole change it makes to the config is three lines, and it writes the file
when an app has none, which a stock `create-expo-app` does not:

```js
const {getDefaultConfig} = require('expo/metro-config');
const {withDesktopPlatforms} = require('basalt-core/metro-config');

module.exports = withDesktopPlatforms(getDefaultConfig(__dirname));
```

All three desktops by default, because `package.json` is committed: which
desktops an app builds for belongs to the project rather than to whoever set it
up, and installing for only the machine at hand would configure an app on a Mac
that failed on a contributor's Linux box. The other two cost source that is
never compiled, the build configures only the host it is running on. An app
that wants fewer says so in `app.json`:

```json
{"basalt": {"desktops": ["macos", "linux"]}}
```

To find out whether a machine can build any of it:

```
npx basalt-core doctor
```

The same checks with writing off, plus the ones `init` has no fix for,
cmake, ninja, GTK's development headers, vcpkg, a React Native outside
`supported-versions.json`. Every one of those otherwise arrives as a compiler
error twenty minutes into a first build, which compiles Hermes and React
Native's C++ from source. A desktop this machine cannot answer for is reported
as unchecked rather than as fine.

That is what this project exists to show.

Beyond the template, a real app's dependency set: expo-image, expo-font,
expo-constants, expo-clipboard, gesture-handler, Reanimated, react-navigation
and the rest, loads **fifteen of fifteen**, identically on both desktops.

`expo-clipboard` really does write the system clipboard, `Constants.expoConfig`
really is the app's `app.json`, and `Linking.createURL` produces the app's own
scheme. That works because `globalThis.expo.modules` is a registry a C++ module
can be added to now, and because the bundler writes the resolved Expo config
beside the bundle the way Expo's own native builds embed it. See

`expo-image` renders too, which took the other half of the port: an Expo *view*
is a Fabric component with a view config in front of it, and phase 36 built that
seam. **react-native-gesture-handler** works as well, taps and pans through
the real library, on both desktops, which took writing its recognisers rather
than compiling them, since it ships no portable C++ (phase 37).

**Reanimated** works too, worklets and all: a pan gesture whose callbacks are
worklets drives a shared value on the UI thread, `useAnimatedScrollHandler`
divides a scroll offset, and neither causes a React re-render. Both packages
ship portable C++, so that port was compiling their sources and supplying the
four things they expect from a platform: plus one gap in React Native's own
cxx platform, which never calls `Scheduler::reportMount` and so leaves every
mount hook inert (phase 38).

## Supported React Native versions

| Version | State |
|---|---|
| 0.87.x | supported, verified against 0.87.1, and what CI builds |
| `main` | supported, and where development happens |
| everything else | refused, with a message saying so |

`packages/basalt-core/supported-versions.json` is the source of truth,
and it pins a triple: this platform, a React Native, and the Hermes to build for
it, chosen and tested together. Bootstrap reads it and stops in seconds on an
unsupported version rather than failing three compile errors deep.

That pinning is not caution, it is the shape of the problem. Hermes and React
Native's C++ host layer both track `ReactCommon` closely enough that one version
cannot serve several React Natives, and this is the only platform that builds
Hermes from source rather than consuming a prebuilt one. `docs/PORTING.md`
has the evidence; `docs/PORTING.md` covers what supporting a release
took in the first place.

## Status

Verified, on screen:

- **`<ScrollView>` scrolls.** Content is clipped to the viewport, `scrollTo` and
  `scrollToEnd` work, and `onScroll` reports `contentOffset` back to React,
  which the demo renders. The offset is also written back into `ScrollViewState`
  on every scroll, which is what keeps `measure`, hit testing and view culling
  honest.
- **`<Image>` loads and paints.** Files, http(s) and base64 data URIs, decoded
  off the main thread, with `resizeMode` cover, contain, stretch and center.
- **`<TextInput>` accepts typing, and it is genuinely controlled.** A real
  `GtkText` does the editing, so input methods, selection, the clipboard and
  every Linux keybinding come with it. Typing reaches React through `onChange`,
  the value comes back down as a prop, and `focus`, `blur` and
  `setTextAndSelection` all work. Verified by typing into it through an X
  server, not by reasoning about it.
- **Input works.** A press on a `<Pressable>` runs GTK hit test, touch event,
  event beat, React Native's responder system, `onPress`, `setState`,
  re-render, mutations, widgets.
- **Text renders and measures** through Pango, so Yoga sizes paragraphs the way
  it does on iOS and Android, and narrowing the window re-wraps them.
- **React runs, from Metro, with Fast Refresh.** Editing `js/index.js` while the
  app is running updates it in place, verified by watching the change land in
  the dumped widget tree. Development is `scripts/metro.sh` plus
  `BASALT_DEV=1`; a `--dev` bundle on disk is not a development mode and will
  not load, which is why `scripts/bundle.sh` produces a production bundle.
- **It works on Linux, not only on the Mac it is developed on.** Verified on
  Ubuntu 24.04 arm64: 56/56 unit tests, the demo rendering correctly, and the
  end-to-end suite passing with *real* pointer and keyboard events through the X
  server rather than injected ones.
- **`<View>`'s prop surface is largely complete.** Border radii (elliptical, per
  corner), per-edge border widths and colours, `transform`, `zIndex`,
  `overflow` and `display: 'none'` all work. A transform is composed during
  layout rather than at paint time, so hit testing follows it.
- **Accessibility.** `accessibilityRole`, `accessibilityLabel`,
  `accessibilityHint` and `accessibilityState` reach GTK's accessible layer, and
  so AT-SPI. A `<Text>` calls itself a label and an `<Image>` an image without
  the app saying so.
- `mount_harness_gtk` still drives hand-built `ShadowViewMutation`s with no JS
  runtime, and `js/demo.js` still drives Fabric's JSI binding with no React.
- This project's own sources build under `-Wall -Wextra` with zero diagnostics,
  enforced by the build rather than asserted.

None of that is checked by eye any more. `build/basalt_gtk_tests` covers the mutation
walk, the widget layer, text measurement, hit testing, the image loader, text
input and accessibility without a JavaScript runtime; `scripts/integration_test.py` runs
the real host against the real bundle and asserts on the widget tree it dumps.
On Linux it drives the host with real pointer events, so GDK's own delivery is
covered too. See `docs/TESTING.md` for what is still not.

Not yet done:

- **`<TextInput>` is a subset.** No `multiline`, no `selection` prop, no
  `onKeyPress`, no `onSelectionChange`, and no shared focus registry, so
  `TextInput.State.currentlyFocusedInput()` is not there. The JavaScript side is
  this project's own file rather than React Native's, which branches on
  `Platform.OS` being exactly `'android'` or `'ios'`; see
- **No keyboard focus for anything else.** A `<TextInput>` takes focus because
  GtkText does, but `<Pressable>` and friends are not reachable by Tab, so a
  screen reader can read the interface and not drive it.
- **No hover**, so `onMouseEnter`-style callbacks do nothing.
- **No scroll momentum**, so `onMomentumScroll*` never fire.
- **No LogBox**, so a JavaScript error is a log line and a window that keeps
  sitting there.
- **`Share`, incoming URLs and desktop notifications** are the core-module gaps
  left. `Appearance`,
  `Blob`/`File`/`FileReader`, `Clipboard`, `Vibration`, `Alert`, `Linking`,
  `I18nManager` and `AccessibilityInfo` all work on both desktops as of phases
  30 to 32; nothing a real app imports throws any more.
- **Nothing desktop-shaped.** One window, no menus, no native file dialogs, no
  drag and drop, no tray. React Native has no cross-platform API for any of it,
  so each is a design decision before it is an implementation.
- **Nothing is published yet.** An Expo app can install these packages and get
  a desktop: `run-linux --build` and `run-windows --build` build the host
  against the app's installed React Native, but setting that up is manual, and
  the first build compiles Hermes from source. And no third-party native module
  has been ported end to end, so what porting one costs is still unknown.

See `docs/BACKLOG.md` for the per-component detail.

## Testing

    ./build/basalt_gtk_tests            # unit, Linux
    ./build/basalt_appkit_tests         # unit, on a Mac
    ./build/basalt_win32_tests.exe      # unit, on Windows
    scripts/integration_test.py         # end to end, needs a built bundle
    node --test scripts/test_cli.js     # the three run commands

The GTK ones need a display; on a headless machine prefix with `xvfb-run -a`.
CI runs them on Linux, where the end-to-end suite drives the app with real
pointer events rather than injected ones. The other two suites need neither a
display nor a window, and each is built only where its toolkit exists. The
end-to-end suite runs against whichever host is built, which on Windows is four
of its five scenarios. See `docs/TESTING.md`.

## Running it

From an app, once a host binary exists:

    react-native run-linux
    react-native run-macos
    react-native run-windows

One command with three names: everything a desktop run does is the same, so it
is written once in `basalt-core/cli/desktop.js` and each platform
package passes in four strings. It starts a packager if one is not running,
launches the app, and stays attached. `--build` builds the host first, and
without it the command says how to build one when it cannot find one.

`examples/demo` is a small app that exercises exactly this, and
`examples/demo/setup.sh` links it against a React Native checkout. It links all
three platform packages even on a machine that can only run one of them, so
`npm run windows` on a Mac reports what it looked for rather than not existing.

Directly, without the CLI. Build a bundle once, then run:

    scripts/bundle.sh ../react-native      # production; see below
    ./build/basalt_gtk

Or against Metro, which is how to develop, and the only way to get Fast Refresh:

    scripts/metro.sh ../react-native          # one terminal
    BASALT_DEV=1 ./build/basalt_gtk      # another

`scripts/bundle.sh` writes a production bundle. A `--dev` one loads and runs
too, which it did not before phase 32: it pulls in LogBox, which reads the
`DevSettings` TurboModule at import time, and `ReactCxxTurboModuleProvider`
serves that module only when a dev server exists. This project now supplies one
outside dev mode, so that import no longer takes the app down. What a `--dev`
bundle still is not is a development *mode*: nothing reloads it, nothing
refreshes it, and LogBox's own images are not among the assets copied next to
it. Use Metro for that.

If Metro cannot build, the host says so and exits rather than starting: the
error you see is Metro's own, code frame and all.

Arguments are `basalt_gtk [bundle] [moduleName]`, defaulting to
`build/main.jsbundle.js` and `BasaltDemo`. An **empty** module name starts a
surface without calling `AppRegistry`, which is the raw-Fabric mode `js/demo.js`
uses:

    ./build/basalt_gtk js/demo.js ""

Environment:

    BASALT_DEV=1            load from Metro; enables Fast Refresh and DevSettings
    BASALT_DEV_HOST/_PORT   where Metro is (default localhost:8081)
    BASALT_DEV_ENTRY        Metro entry name without extension (default "index")
    BASALT_QUIT_AFTER_MS    quit on a timer, for automation that knows in
                              advance how long it needs. SIGINT and SIGTERM
                              also shut down cleanly, so Ctrl-C and `kill` run
                              GApplication::shutdown -- stopAllSurfaces, the
                              teardown ordering, and the tree dump -- rather
                              than dropping the process where it stands.
    BASALT_TEST_TAP         "x,y;x,y" -- synthesise taps a second apart, in
                              surface-root coordinates. Enters where GTK's
                              gesture callback would, so it exercises hit
                              testing and event delivery but not GDK itself.
    BASALT_TEST_TYPE        text to insert into whatever field has focus,
                              after the taps above. Enters through GtkEditable,
                              so it skips the key controller and the input
                              method and exercises everything above them.
    BASALT_DUMP_TREE        write the widget tree to a file on the way out.
                              Useful on its own for seeing what React actually
                              produced, and what the end-to-end tests assert on.

### On a Mac

`build/basalt_appkit` is the same host over AppKit, and takes the same arguments.
It mounts `<View>` and nothing else so far, and it defaults to an **empty**
module name rather than `BasaltDemo`, because a React app with any text in it
would render blank rectangles:

    ./build/basalt_appkit js/demo.js

Its environment variables are `BASALT_*` in place of `BASALT_*`:
`BASALT_DEV`, `BASALT_DEV_HOST`, `BASALT_DEV_PORT`, `BASALT_DEV_ENTRY`,
`BASALT_QUIT_AFTER_MS`, `BASALT_DUMP_TREE`, plus `BASALT_SNAPSHOT`, which writes
a PNG of what is actually on screen. Two prefixes for the same knobs is an
inconsistency that should become one; it has
not yet.

To check the two agree:

    scripts/compare_hosts.sh

which runs a script through both and diffs the tree each produced.

## The `linux`, `macos` and `windows` platforms

`packages/basalt-core` is the JavaScript half: the `Platform` module and
the Metro configuration that makes Metro resolve it. With it, an app bundled
with `--platform macos` sees `Platform.OS === 'macos'` and can use `.macos.js`
files, in development and in a release build alike. The names match
react-native-macos and react-native-windows, so a library that already ships
`Button.macos.js` for those forks resolves correctly here.

In an app's `metro.config.js`:

    const {withDesktopPlatforms} = require('basalt-core/metro-config');
    module.exports = withDesktopPlatforms(config);

`withLinuxPlatform` still exists and still enables only `linux`.

Almost none of what this does is per-platform: the shims below resolve to the
same place on all three, and only `Platform` differs, by one string. See

Getting there is mostly about a family of React Native files that cannot work on
a platform React Native has never heard of:

- **Self-importing shims.** Nine files whose entire body is
  `import X from './X'; export default X;`, there so deep imports keep working
  and relying on a platform-specific sibling winning the resolution. On a new
  platform each resolves to *itself* and exports undefined. They fail one at a
  time, far from the cause: `Platform.constants` is undefined, then a view
  config is undefined, then a component is undefined. Each is answered with its
  own `.android.js` sibling, because this platform reports
  `PlatformConstantsAndroid` from C++ and shares ReactCommon's prop parsing, so
  Android's implementation is the one that matches what is really here.
- **Modules with no neutral file at all**, which fail to resolve rather than
  resolve wrongly, and only in development bundles.
- **`Platform` itself**, which is the one thing this platform genuinely
  implements differently.

One thing the C++ side cannot be told: `DevServerHelper` builds its bundle URL
from a hardcoded `platform=android`, with no hook and no setting. Left alone an
app would be `android` under Fast Refresh and `linux` in release, which is a
worse trap than either on its own, so the Metro config rewrites the request on
arrival.

## Building React Native's core

The closure of `react_renderer_mounting` is 29 RN targets, computed from RN's
own `target_link_libraries`. `native/cmake/ReactNativeCore.cmake` adds them;
`cmake/ThirdParty.cmake` supplies the third-party target *names* RN links
against, since a host build has no gradle step to download them.

Six things a host has to get right, none of them documented:

1. **Use clang, not GCC.** RN builds with `-Wall -Werror -Wpedantic` and is a
   clang codebase. Under GCC it fails on `#pragma mark` (Xcode-ism), on folly's
   `__int128` under `-Wpedantic`, and on `-Wsubobject-linkage` in
   `NetworkIOAgent`. Under clang all of it compiles.
2. **`-latomic`.** `std::atomic<std::optional<double>>` in
   `ReactNativeFeatureFlagsAccessor` is 16 bytes wide; those are out-of-line
   calls. Fantom's `CMakeLists.txt` carries the same `if(UNIX AND NOT APPLE)`
   branch.
3. **Platform-variant include dirs must be global.** Each seam with a
   `platform/{android,ios,cxx}` split needs its variant on the include path
   before any RN target is added, or targets like `componentregistry` cannot
   find `HostPlatformViewProps.h`.
4. **OBJECT libraries do not propagate objects through an INTERFACE target.**
   `target_link_libraries` carries usage requirements only; the objects need
   `$<TARGET_OBJECTS:...>` naming explicitly, or the link comes up short on
   every symbol they define.
5. **`getDefaultComponentRegistryFactory()` is not implemented anywhere in
   ReactCommon.** It is declared, and no `.cpp` in the tree defines it: each
   host writes its own, and in doing so declares which components its platform
   supports. See `native/src/LinuxComponentRegistry.h`. Fantom has its own version.
6. **`JSIDynamic.cpp` is compiled by no CMakeLists in the tree**, yet
   `RawProps` references `jsi::dynamicFromValue`. Hosts must build it.
7. **`getHttpClientFactory()` is declared but defined nowhere**; a host must
   implement `IHttpClient` (one method). And `getWebSocketClientFactory()` *is*
   defined, in `react/http/platform/cxx/WebSocketClient.cpp`, which the
   `react_cxx_platform_react_http` target does not glob. Hosts compile it.
8. **Order matters in CMake variable assembly.** `set(RN_CORE_OBJECT_TARGETS
   ...)` after a `list(APPEND ...)` silently discards the appended entries, and
   the failure surfaces much later as missing objects at link time.

### Upstream bug found

`ReactCommon/jsinspector-modern/network/HttpUtils.h` uses `uint16_t` without
including `<cstdint>`. It builds on Meta's toolchains only via transitive
includes. Worked around with a force-include rather than patching the checkout.

### Version drift

RN pins glog `0.3.5`, boost `1_83_0`; Arch ships glog `0.7.1`, boost `1.92.0`.
glog >= 0.6 added a guard requiring `GLOG_USE_GLOG_EXPORT` when its headers are
not consumed through its own CMake target, which RN's pin predates, without
it every glog header hard-errors. Boost 1.92 caused no problems.

### Note on rendering

Under Hyprland with Omarchy's window rules the window is composited
translucently, so screenshots show the wallpaper through the views. That is a
compositor opacity rule, not the renderer.

## Next

In the order it is likely to be done. An entry with a proposal behind it names
it; the rest are not designed yet. The per-area detail is `docs/BACKLOG.md`; how
the thing is built is `docs/ARCHITECTURE.md`.

1. **Fast Refresh end to end in CI.** It works on all three desktops,
   observed, including on Windows through `run-windows`, which is what phase 43
   was for. What it is not is *tested* by any machine that is not somebody's
   laptop. Half of that is now fixed: the scenario no longer skips on Windows,
   because `scripts/metro.js` starts the packager in Node rather than in shell.
   The other half is that CI sets `BASALT_SKIP_FAST_REFRESH` on all three jobs,
   because Metro on a GitHub runner never notices an edit: the file changes
   with a fresh mtime, a freshly requested bundle still carries the old text,
   and Metro logs nothing. That variable now drops the *edit* rather than the
   scenario, so CI does guard the host half: dev mode, the dev server helper,
   the websocket, `DevSettings`, and a bundle served by Metro rather than read
   off disk. What is left is the edit itself, which needs a runner whose file
   watching works; `docs/BACKLOG.md` has what remains to try. In progress, as
   `openspec/changes/test-fast-refresh-on-windows`.
2. **A first release run.** `release.yml` has never run: its macOS job, Linux
   and Windows built from nothing, and the Expo install job are all unproven on
   hosted runners. Deliberately not yet. Windows belongs in the install job
   before it does.
3. **A faster first `--build`.** It compiles Hermes and React Native's C++
   from source, twenty to thirty minutes. Prebuilt Hermes per platform and
   React Native version is what would change that for someone trying it out.
4. **Publishing.** Versions, an npm account, the publish step in
   `release.yml`, and an install guide.

Known, and waiting on upstream rather than on this list: on React Native 0.86,
which current Expo installs, a full reload tears the host down (fixed in 0.87;
see `supported-versions.json`), so an edit Fast Refresh cannot apply in place
closes the app.

macOS is verified again as of 2026-09-14, on an Apple Silicon Mac: the whole
tree builds with no diagnostics, both unit suites pass, `integration_test.py`
passes all five scenarios including Fast Refresh, and `compare_all.sh` agrees
with Linux on all twelve apps, borders and radii included, which that dump
could not previously express.

The guess this paragraph used to make was wrong, and it is worth keeping the
correction. Fast Refresh failing on macOS was blamed on the dev script URL in
`main_appkit.mm` naming `linux`; that had already been fixed and was not the
cause. The cause was this project's own `SourceCode` module reporting a
synthesised bundle URL, which named a Metro *graph* nobody had built, and
Linux was not exempt, only lucky, because the end-to-end suite happened to
build that graph before the client asked for it.

## Caveat

`ReactCxxPlatform` carries no public API stability guarantee, Fantom builds it
with `RN_BUILDING` to reach private includes. It will churn. The mitigation is
that Meta keeps it working for their own CI.
