# Handoff

Picking this project up on another machine, specifically an Apple Silicon Mac.
That move is done: as of 2026-09-08 the project builds and runs there. See
[macOS: verified](#macos-verified-2026-09-08) for what had to change.

## Where things stand

| Phase | Deliverable | State |
|---|---|---|
| 0 | GTK view layer, mounting manager | done |
| 1 | Real Fabric mutations → GTK widgets, no JS | done, verified on screen |
| 2 | RN's full C++ core + Hermes + codegen building | **done**, on Linux and macOS |
| 2 | `main.cpp`: construct `ReactHost`, run a surface | done, verified on screen |
| 3 | Metro bundle, React, Fast Refresh | done, verified on screen |
| 4 | Pango text: `<Text>` measures and renders | done, verified on screen |
| 5 | Input: GTK events into RN's touch events | done, verified on screen |
| 6 | `<Image>` and `<ScrollView>` | done, verified on screen |
| 7 | Accessibility, and a test suite | done |
| 8 | The `linux` Metro platform | done |
| 9 | `<TextInput>`, keyboard and focus | **next** |
| 10 | `run-linux` CLI, packaging | |

`Platform.OS === 'linux'` now, in development and release alike. `<TextInput>`
is no longer blocked on the platform, but still needs keyboard input and a
focus model. See `docs/DECISIONS.md`.

87 React Native targets build (ReactCommon + ReactCxxPlatform, including
`ReactHost`), plus Hermes and codegen. `basalt_gtk` runs a real React Native
app: Metro bundles `js/index.js`, `AppRegistry` starts the surface, React
reconciles, Yoga lays out, and the mutations reach GTK4 widgets through
`GtkMountingManager`. Fast Refresh works against a running Metro.

`<View>`, `<Text>`, `<Image>` and `<ScrollView>` all have GTK peers, and presses
reach React through the responder system, so an app is interactive and can
scroll. Accessibility properties reach AT-SPI. What is missing is still the component
surface rather than the runtime: no `<TextInput>`, and no keyboard or focus
model.

There are tests now, `build/basalt_gtk_tests` and `scripts/integration_test.py`. See
`docs/TESTING.md`, which also has the recipe for running everything on Linux in
a VM, which is where it should be checked before being believed.

## Setup

```bash
git clone https://github.com/vincentvella/basalt-core
cd basalt-core
git clone --depth 1 https://github.com/react/react-native ../react-native
scripts/bootstrap.sh ../react-native
```

Already have a React Native checkout? Skip that clone and point bootstrap at
it (`scripts/bootstrap.sh /path/to/react-native`). Two caveats: bootstrap runs
`yarn install` there if `node_modules` is empty, which rewrites its
`yarn.lock`; and everything here is built against RN `main`, so an older tagged
release will likely need adjustment, `ReactCxxPlatform` carries no API
stability guarantee.

`bootstrap.sh` fetches folly/fast_float/nlohmann_json, builds Hermes, and runs
React Native's codegen. It is idempotent; `--force` redoes everything. It caps
build parallelism at `BUILD_JOBS` (default 12) deliberately; a full-width
build makes a laptop unusable and pins the fans well past the end of the build.

Then:

```bash
cmake -B build -G Ninja -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DRN_DIR=../react-native/packages/react-native
nice -n 10 cmake --build build -j 12
./build/mount_harness_gtk
```

## macOS: verified 2026-09-08

Built and run on an Apple Silicon Mac (macOS 26, AppleClang 21, Homebrew GTK4
4.22.4). `mount_harness_gtk` renders both transactions correctly on the quartz
backend: the frames, the recolour, the removal and the overflowing nested child
all match what the mutations describe. `demo_layout_gtk` renders too. Four things
needed fixing, all now in the repo:

- **`bootstrap.sh` died under bash 3.2**, which macOS ships. `"${arr[@]}"` of an
  empty array is an unbound-variable error under `set -u` before bash 4.4. The
  script now uses the `${arr[@]+"${arr[@]}"}` idiom.
- **`-DRN_DIR=../react-native/...` broke RN's own `include()` calls**, which
  resolve a relative path against the including file's directory. The top-level
  `CMakeLists.txt` now absolutises `RN_DIR` against the repo root.
- **`third_party/codegen/CMakeLists.txt` had never been committed.** The README
  called it checked in, but `.gitignore` covered the whole directory, so it only
  existed on the Arch box. It is recreated from Fantom's and now un-ignored.
  Fantom's glob is right for a reason: the top-level
  `FBReactNativeSpec-generated.cpp` includes `ReactCommon/JavaTurboModule.h`,
  which is Android-only. Only `react/renderer/components/FBReactNativeSpec/*.cpp`
  is compiled.
- **Homebrew headers are not on the default include path.** The glog, fmt and
  double-conversion interface targets in `cmake/ThirdParty.cmake` carried only
  the library, which is fine when headers live in `/usr/include`. They now carry
  `find_path` results too.

`-DCMAKE_PREFIX_PATH="$(brew --prefix)"` was **not** needed: CMake finds
`/opt/homebrew` on its own.

### Dependencies

```bash
brew install gtk4 pango glib glog boost fmt double-conversion gflags \
             openssl cmake ninja pkg-config
```

Only `gtk4` was missing on the first Mac. Node came from nvm (24.x); mise is
optional, bootstrap falls back to the ambient node and checks its major version
against RN's engines (`^22.13 || ^24.3 || >= 26.0.0`; **Node 25.x is
excluded** and fails an engine check inside a preinstall hook). Bootstrap
installs yarn globally into that node if it is absent.

### Confirmed as predicted

- `-latomic` guard, `GLOG_USE_GLOG_EXPORT`, Apple clang, and the Hermes host
  flags all behaved as the reasoning above expected. Hermes needed no changes;
  its 398 objects compile in well under a minute at `-j 12`.
- `GtkAnimationChoreographer` links and constructs; whether the quartz frame
  clock ticks with the same fidelity as Wayland's is still unverified, since
  nothing animates yet.

### Screenshotting a GTK window on macOS

`screencapture -l <window-id>` needs a CGWindow id, which nothing in the shell
exposes. A five-line Swift program over `CGWindowListCopyWindowInfo`, filtered
by `kCGWindowOwnerPID`, gives it; compile it once with `swiftc` because
`swift <file>` interprets slowly enough to miss the window's first frame.

## Verified on Linux, 2026-09-09

Everything from phase 2 onwards had only ever run on macOS against the quartz
backend. It now also runs on Ubuntu 24.04 arm64, in a Lima VM using Apple
Virtualization, with clang 18, GTK 4.14.5 and Pango 1.52.1.

Two portability bugs turned up immediately, both invisible on macOS:

- `bootstrap.sh` did `npm install -g yarn`, which needs root on a stock Ubuntu
  because npm's global prefix is `/usr/lib/node_modules`. Homebrew's node has a
  user-writable prefix. yarn now installs under `third_party/`.
- React Native's targets build with `-Werror`, and on Linux clang uses
  libstdc++, whose `std::bind` instantiates the deprecated `std::result_of`.
  libc++ does not, so React Native never sees it.

What was checked: 40/40 unit tests under a headless sway with the pixman
renderer; the demo rendering correctly, captured with `grim`; and the
end-to-end suite passing with real pointer events through an X server, which
also confirmed the wheel path converts notches to pixels exactly as intended.

Wayland *input* is still unverified; a headless compositor has no seat, so
there is no pointer to move. That needs a desktop session or real hardware.

## A threading bug worth remembering, 2026-09-09

`GtkMountingManager::dispatchCommand` was calling into GTK from the JavaScript
thread. It had always been wrong and had never mattered, because the only
commands were `ScrollView`'s and moving an adjustment touches nothing
reentrant. `<TextInput>`'s `focus` reaches the platform input method, and on
macOS AppKit asserts it is on the main thread and traps the whole process.

Two things are worth taking from it. The bug was found on *macOS*, which
inverts the pattern every other portability bug in this project has followed,
so "it only breaks on Linux" is not a rule. And it was latent for two phases:
GTK is not thread-safe either, so the Linux builds were relying on luck rather
than on being correct. Anything reached from a scheduler delegate callback
should be assumed to be on the JavaScript thread until proven otherwise.

## The honest caveat about the platform

This is a *Linux* platform. Developing it on macOS means testing against a GTK
backend that is not the target: Wayland fractional scaling, client-side
decorations, portals and compositor behaviour are all different or absent.

That matters more than it might sound. `react-native-gtkx`'s author develops
from macOS via a Linux VM, and native Linux testing was a genuine differentiator
for this project. If the Mac becomes the primary machine, a Linux VM (or the
Arch box over SSH) is worth keeping in the loop for anything touching layout,
input, scaling or accessibility.

## Working notes

`docs/BACKLOG.md` and `docs/backlog/` carry the known gaps, one file per area.
`docs/DECISIONS.md` records why Path A, why clang, and why `RnLayout` does no
layout. `docs/PORTING.md` has what it takes to build React Native's core
somewhere new: the Hermes and MSVC fixes, and which React Natives are
supported. Work being proposed or under way is `openspec/changes/`; what the
platform is required to do is `openspec/specs/`.

## Not in this repo

Everything `bootstrap.sh` regenerates: `third_party/` (folly, fast_float,
nlohmann_json, hermes, hermes-build, codegen) and `build/`.

Two bits of machine state also do not travel, both handled by `bootstrap.sh`:
React Native's `node_modules` (its `yarn install` also rewrites `yarn.lock` in
the RN checkout), and a Node version satisfying RN's engines.
