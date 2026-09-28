# An app's own native module, and an Expo module that can emit

## Why

kino has two Expo modules of its own -- `KinoProcess`, which spawns and watches
the editor's daemon, and `KinoAudio` -- written as local modules under
`apps/desktop/modules/`, which is where `create-expo-module --local` puts them and
where Expo's autolinking looks. Each has an Apple half and nothing for a desktop,
and until now there was nowhere to put one: a capability package is discovered
from the app's `dependencies`, and publishing a package to yourself so that one
binary can compile a file already in the repository is not a reasonable thing to
ask.

`KinoProcess` then turned out to need something the platform did not have at all.
Every Expo module here was synchronous underneath -- a clipboard read, a URL check
-- so already-settled promises were the honest answer and nothing ever needed to
reach JavaScript later. A subprocess does: its output arrives on a reader thread
minutes after `spawn` returned, and an exit code arrives after that. A
TurboModule is handed a `CallInvoker` for exactly this; an Expo module is built
from a function given a runtime and nothing else, so it had no way to emit an
event at all.

Without both, "an Expo app runs as expected, and installing basalt plus the
basalt modules it needs unlocks the desktop" stops being true for any app whose
own native code does more than answer immediately.

## What Changes

- An app's local native modules -- `<projectRoot>/modules/*` with a
  `native/CMakeLists.txt` -- are compiled into the host, on the same contract as
  a capability package. Being there is the declaration, because a local Expo
  module has no manifest to declare it in.
- Anything in core can reach the JavaScript thread, through a runner each host
  leaves behind. An Expo module can therefore emit an event, and settle a promise
  later, without being a TurboModule.
- The portability probe answers for the file-dialog and share seams, which it had
  never been asked about because nothing in core's link closure referenced them.

Calling `fetch` was the next thing to fail, and for a related reason: Expo
replaces the global with its own implementation over `ExpoFetchModule`, which is
not ported, and does it as a lazy getter — so the failure lands on the app's
first call rather than at import. That is one line to fix and was worth fixing
here, because an Expo app that cannot call `fetch` is not running whatever else
works.

## Capabilities

### Modified Capabilities
- `expo-runtime`

## Impact

- `native/core/JsRuntimeAccess.{h,cpp}`: the runner, and a `CallInvoker` over it
  whose `invokeSync` refuses rather than deadlocking.
- `native/core/ExpoModules.{h,cpp}`: `emitExpoEvent`, which finds the module by
  name at the moment of the emit rather than holding one across a reload.
- The three host mains, which leave the runner as they start and clear it as they
  stop -- beside the event-listener installer they already leave.
- `cli/desktop.ts`: `localNativeModules`, and a failed build that names an app's
  own module as a contributor.
- `native/core/portability_probe.cpp`: two seams a new port owes and was not told
  about.
- `native/core/ExpoRuntime.cpp`: `useReactNativeFetch`, which sets expo's own
  `EXPO_PUBLIC_USE_RN_FETCH` before the bundle so that `fetch` works.

## Not built

- **Windows, for kino's module.** Not this change's to build: the module is
  kino's. It is recorded here because the reason is the platform's shape rather
  than effort. `CreateProcess` with a pair of pipes is the smaller half; the
  larger is that the command a caller passes is a POSIX shell command -- an
  inline environment assignment and an `exec`, which cmd.exe has neither of -- so
  the command has to become portable before the spawning can.
- **`ExpoFetchModule` itself.** The default above routes around it rather than
  porting it. Porting it means a pair of SharedObject-derived native classes with
  per-instance events — which `emitExpoEvent` does not cover, because it finds a
  module by name and a `NativeResponse` is an instance — plus a streamed body.
  Until then `expo/fetch`, imported directly, still reports the module is
  missing, and a response body is buffered rather than streamed.
- **An Expo event in basalt's own end-to-end suite.** basalt has no Expo module
  of its own that emits, so the runtime half of `emitExpoEvent` is proven by
  kino's daemon and by nothing in this repository. What is covered here is the
  decision: whether there is a runner, whether work reaches it, and what happens
  when a host has gone. A module in basalt that emits would be the thing worth
  testing, and there is not one yet.
