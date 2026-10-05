# Tasks

## 1. The JavaScript thread, for code that is not a TurboModule

- [x] 1.1 `core/JsRuntimeAccess.{h,cpp}`: `setRuntimeRunner`, `runOnJsRuntime`,
      and `jsCallInvoker()` over it.
- [x] 1.2 `invokeSync` throws with a self-naming message rather than pretending
      it can wait.
- [x] 1.3 Each host leaves the runner as it starts, from
      `ReactHost::runOnRuntimeScheduler`, and clears it before it destroys the
      host.
- [x] 1.4 `tests/test_js_runtime_access.cpp`: no runner, a runner, a cleared
      runner, and the invoker's refusal.

## 2. Emitting

- [x] 2.1 `emitExpoEvent(module, event, payload)`, looked up by name at the
      moment of the emit.
- [x] 2.2 A no-op when there is no Expo, so a package's portable half can emit
      without a guard.
- [ ] 2.3 The runtime half, in an end-to-end scenario. Needs an Expo module in
      basalt that emits; see the proposal's "Not built".

## 3. An app's own modules

- [x] 3.1 `localNativeModules(projectRoot)`: `modules/*/native/CMakeLists.txt`,
      sorted so two machines configure the same build.
- [x] 3.2 Configured through the same `-DBASALT_PACKAGES` the packages use.
- [x] 3.3 Named as a contributor when the build fails.
- [x] 3.4 `scripts/test_cli.js`: a module with a desktop half, one without, an
      app with no `modules` directory, both lists reaching one -D, and the
      failure note.

## 3b. What a contributing module links

- [x] 3b.1 `BASALT_PACKAGE_{APPKIT,GTK,WIN32}_LINK_LIBRARIES`, read by each host
      and applied to the host and its tests.
- [x] 3b.2 The notifications package declares its own `-framework
      UserNotifications`, which the AppKit package used to name for it. Moved so
      that the property is exercised by this repository's own CI rather than only
      by an app outside it.

## 4. `fetch`

- [x] 4.1 `useReactNativeFetch`: set `EXPO_PUBLIC_USE_RN_FETCH` before the
      bundle, as a default a developer can beat.
- [x] 4.2 `e2e/expofetch.js` and a scenario asserting the value arrives and
      `fetch` works. Checked against a build with the call neutered: it fails
      there, and the first attempt to check that -- deleting the call -- proved
      nothing, because `-Werror` on the now-unused function meant the test ran
      against the previous binary.
- [ ] 4.3 The other half -- expo honouring it -- in basalt's own harness. Blocked
      on this repository's bundler not pulling expo's winter runtime into a e2e/
      app; verified by running kino instead.

## 5. The probe

- [x] 5.1 Stub `showFileDialog` and `shareContent`, which nothing in core's link
      closure had referenced.

## 6. Proven against a real app

- [x] 6.1 kino's `KinoProcess` built as a local module: spawn, output events and
      isRunning, verified by the editor's daemon starting and answering.
- [x] 6.2 The parts running the app does not reach -- exit codes, kill, the
      working directory, output ordering -- covered by a test in kino beside the
      module.
- [x] 6.3 kino's `KinoAudio` as a second local module, which is what needed the
      link-libraries property. Its test plays a real file and measures that the
      reported position tracks real time, because preview.ts uses it as a clock.
- [ ] 6.4 Audio on Linux and Windows. Each file says what it would take --
      GStreamer's `playbin`, Media Foundation's `IMFPMediaPlayer` -- and why it
      is not written blind: neither this machine nor CI can tell a working
      implementation from one reporting plausible numbers to nobody.

## 7. What the migration turned up

- [x] 7.1 `resourcePath`, from the platform rather than derived by the app.
- [x] 7.2 Resolution from a nested directory: every `node_modules` up from the
      project root, an `extraNodeModules` fallback, and -- because neither is
      read by every resolver, and the app in question uses one that reads
      neither -- answering for our own names directly in `resolveRequest`.
- [x] 7.3 A linked capability package is watched as well as resolved, or the
      bundler refuses to read it and says "Failed to get the SHA-1 for".
