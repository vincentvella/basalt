# Native modules

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (1):**

1. Four modules an app can reach that throw here, each with no desktop
   equivalent written
2. ~~Nothing runs the audit against a *running* host~~

- **The audit, and why there is one.** The support page answers "does this prop
  work" one row per attribute, and that page caught claims nothing implemented.
  Native modules are the other half of what an app touches --
  `Clipboard.setString`, `Linking.openURL`, `AppState.currentState`,
  `Appearance.getColorScheme`, every `NativeEventEmitter` -- and nothing
  answered the same question for them until 2026-10-10.

  `scripts/audit_modules.py` reads three sources, none of which is a list kept
  by hand:

  - every `TurboModuleRegistry.get` and `.getEnforcing` in React Native's own
    JavaScript, which is what an app can reach by importing `react-native`;
  - each host's provider chain in `main_*.cpp`, resolved through the generated
    specs the classes derive from, because a module class rarely declares its
    own name;
  - React Native's own two providers, `ReactCxxTurboModuleProvider.cpp` and the
    `DefaultTurboModules.cpp` it falls through to.

  `docs/platform-modules.json` is the triage, and `--check` compares the three
  against it. Four kinds of difference are caught, each checked by breaking it:
  a module upstream added that nobody has triaged, a status that claims an
  answer the code does not give, a module nobody answers with nothing written
  about what an app sees, and a status for a module neither side mentions any
  more.

  **Two of those four are not failures on an unpinned checkout.** The file is
  about the React Native in `scripts/react-native.pin`, so the list of modules
  an app can reach is that release's list, and a local `main` has already added
  `NativeResizeObserverCxx`, dropped `ModalManager` and changed
  `NativePerformanceCxx` to `getEnforcing`. Differences in React Native's own
  list are reported as drift and exit zero; differences about *this* repository
  -- a chain that stopped answering, a status claiming an answer nothing gives,
  an unresolved provider class -- fail whichever checkout they were read from.
  The same split `scripts/scrape_props.py` makes between `--from-pin` and
  RN_DIR.

  **The difference from a prop is what happens when nobody answers.** `get`
  returns null and the JavaScript copes; `getEnforcing` throws, so an app dies
  on the line that reached it. That is why the file records which of the two it
  is, per module, rather than only whether it is answered.

  As of 2026-10-10: React Native asks for **60**. This repository answers
  **15** of them, React Native's own two providers answer **19**, and **26** are
  answered by nobody. The hosts answer eleven *further* modules that React
  Native never asks for -- this project's own, and the libraries whose modules
  the hosts offer unconditionally -- which is why the chains hold 26 names each
  and only 15 of those appear in React Native's own JavaScript.

- **Four modules an app can reach throw rather than no-op**, and each is a
  desktop equivalent nobody has written. They are the whole of the first open
  entry:

  | Module | What an app did | What would answer it |
  | --- | --- | --- |
  | `SettingsManager` | `Settings.get`, `Settings.set` | iOS's user defaults. A file, or AsyncStorage, or the platform's own settings store |
  | `ImageEditingManager` | `ImageEditor.cropImage` | A Skia or Core Graphics crop; the hosts all have an image pipeline already |
  | `ImageStoreManager` | `ImageStore.getBase64ForTag` | Deprecated upstream; probably not worth writing |
  | `ActionSheetManager` | `ActionSheetIOS.showActionSheetWithOptions` | A menu or a dialog, both of which `basalt-core` has |

  None of the four is reached by React Native's own code, only by an app that
  imports that API, which is why nothing here has noticed. `PushNotificationIOS`
  is a fifth of the same shape and has a package instead; see
  `packages/basalt-notifications`.

- **The twenty-one others nobody answers are not gaps**, and the audit says so
  per module rather than leaving a reader to wonder. Android's halves of
  cross-platform APIs (`IntentAndroid` for `Linking`, `DialogManagerAndroid` for
  `Alert`, `PermissionsAndroid`, `ToastAndroid`, `HeadlessJsTaskSupport`,
  `DeviceEventManager` for the back button) are not reached off Android, and the
  iOS halves of the same APIs *are* answered here. React Native's own test
  infrastructure (`NativeFantomCxx`, `CPUTimeCxx`, `SampleTurboModule`) is not
  an app's. `JSCHeapCapture` is JavaScriptCore's and this platform runs Hermes.
  `RedBox` is the old LogBox. `Timing` is the legacy timer module, and timers
  come from the runtime scheduler now.

  Two are worth reading twice because the name is alarming and the answer is
  not:

  - **`UIManager`** is asked for with `getEnforcing`, and nothing answers it.
    It is never asked: `UIManager.js` resolves to `BridgelessUIManager` because
    `RN$Bridgeless` is true, which `ReactInstance.cpp` sets unconditionally. A
    library calling a method that implementation does not have gets its own
    error naming the method.
  - **`KeyboardObserver`** is unanswered and deliberately so: `Keyboard.js`
    passes null for the module off iOS, so listeners are registered and never
    fire. A desktop has no soft keyboard to report.

- ~~**Nothing runs the audit against a running host.**~~ Done the same day, and
  it earned its keep immediately.

  `e2e/turbomodules.tsx` -- the app that already asked the proxy about three
  names -- now walks `docs/platform-modules.json` itself and asks the running
  host for every module in it, and the scenario fails on any disagreement. The
  app imports that file rather than carrying a copy of the names, which is what
  keeps the two from disagreeing about which modules exist; `e2e/metro.config.js`
  gained `docs/` as a watch folder for that one import.

  **The first run found three modules the provider chains offer and a plain run
  does not**, which is the whole argument for checking a runtime rather than
  trusting a chain: `DevLoadingView` wants a dev UI delegate,
  `NativeViewTransitionCxx` wants a feature flag that is off by default, and
  `ReactDevToolsRuntimeSettingsModule` wants `REACT_NATIVE_DEBUGGER_ENABLED_DEVONLY`.
  A chain says *that* a module is offered, never *when*. Each of the three now
  carries its condition in the file, beside this project's own three -- Skia,
  Reanimated and worklets, which a host offers only when the build was pointed
  at an app that has the library -- and the check skips anything with a
  condition rather than pretending to know.

  Sixty-five modules agree on GTK and on AppKit; Windows is CI's to confirm.
