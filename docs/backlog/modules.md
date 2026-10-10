# Native modules

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (1):**

1. Two modules an app can reach that answer nothing here, each with no desktop
   equivalent written
2. ~~Nothing runs the audit against a *running* host~~
3. ~~`Settings` is a module nobody answers~~
4. ~~`ActionSheetIOS` has no module, and dies on the first call~~

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
  **17** of them, React Native's own two providers answer **19**, and **24** are
  answered by nobody. The hosts answer eleven *further* modules that React
  Native never asks for -- this project's own, and the libraries whose modules
  the hosts offer unconditionally -- which is why the chains hold 28 names each
  and only 17 of those appear in React Native's own JavaScript.

- **Two modules an app can reach answer nothing**, and each is a desktop
  equivalent nobody has written. They are the whole of the first open entry:

  | Module | What an app reaches it with | What would answer it |
  | --- | --- | --- |
  | `ImageEditingManager` | `react-native/Libraries/Image/NativeImageEditor` | A Skia or Core Graphics crop; the hosts all have an image pipeline already |
  | `ImageStoreManager` | `react-native/Libraries/Image/NativeImageStoreIOS` | Deprecated upstream; probably not worth writing |

  Both are `getEnforcing`, which throws at import -- but only for an app that
  imports the spec file by path, because React Native stopped exporting
  `ImageEditor` and `ImageStore` from its index and nothing in its own
  JavaScript imports either spec. **Measured on `main`, which is not the pin**:
  the shape of the claim is the same on v0.87.1 and the exact answer there has
  not been checked, so what the table names is the import that reaches the
  module rather than an API call.

  Neither is reached by React Native's own code, only by an app that goes
  looking, which is why nothing here has noticed. `PushNotificationIOS` is a
  third of the same shape and has a package instead; see
  `packages/basalt-notifications`.

- ~~**`Settings` is a module nobody answers.**~~ Done on all three on
  2026-10-10, and it took a JavaScript override as well as a module, which is
  the third time that pair has been the answer -- `Share`, `Alert`, and now
  this.

  **The entry it came from was wrong in the app's favour, and the audit was the
  thing that was wrong.** `SettingsManager` is a `getEnforcing` lookup, so the
  file recorded `Settings.get` and `Settings.set` as *throwing*. They did not.
  `Settings.js` branches on `Platform.OS === 'ios'` and hands everything else
  `SettingsFallback`, whose four methods `console.warn` and answer null, so the
  module was never reached and nothing threw: an app's settings were silently
  dropped, one warning per call, which reads like a platform limitation rather
  than a missing module. Measured by sabotage rather than by reading -- putting
  React Native's own `Settings.js` back produces
  `Settings is not yet supported on this platform` and a null, twice per call.
  A `getEnforcing` in a spec says what happens *if* an app reaches the module,
  and says nothing about whether any of React Native's own JavaScript will.

  **A file, not this desktop's own settings store**, and the three are not
  interchangeable: macOS has `NSUserDefaults`, which is the thing `Settings`
  wraps; GTK's nearest equivalent is `GSettings`, which refuses a key that is
  not in a compiled schema, and this API's whole contract is that an app invents
  its keys at runtime; Windows has the registry, which has no JSON and no
  arrays. So it is one JSON object under the per-user configuration directory on
  all three -- `$XDG_CONFIG_HOME` or `~/.config`, `~/Library/Application
  Support`, `%APPDATA%` -- in a directory named after the app's identifier,
  written through a sibling and renamed over so a process that dies mid-write
  leaves the previous file. See `core/SettingsStore.h` for the argument and
  `src/overrides/Settings.ts` for what the JavaScript half had to do.

  Two things are left, and both are recorded rather than forgotten:

  - **`watchKeys` is registered and never fires.** That is iOS's shape, not a
    stub: `Settings.set` merges into JavaScript's own copy before calling the
    module, so `_sendObservations` sees no change -- which is why
    `RCTSettingsManager` sets `_ignoringUpdates` around its own `setValues`
    instead of emitting. What fires a watcher is a change from *outside* the
    process, which means watching the file: `GFileMonitor` through
    `g_file_monitor_file` on Linux, `NSUserDefaultsDidChangeNotification` on
    macOS if it moved to `NSUserDefaults`, and `ReadDirectoryChangesW` or
    `FindFirstChangeNotification` on Windows. One listener each, delivering
    `settingsUpdated` with the file's new contents, and the JavaScript half
    already handles the rest.
  - **macOS settings are not visible to `defaults read`**, because they are not
    in `NSUserDefaults`. `-[NSUserDefaults persistentDomainForName:]` for the
    snapshot and `-setPersistentDomain:forName:` for the write would change
    that, for one host, at the cost of the three agreeing about what a settings
    value can be.

- ~~**`ActionSheetIOS` has no module, and dies on the first call.**~~ Done on
  all three on 2026-10-10, and it is the shape the other three were not:
  `ActionSheetIOS.js` has no `Platform.OS` branch anywhere in it, so the module
  alone was the whole gap. An iOS-named API is not automatically one whose
  JavaScript refuses to run off iOS, and the only way to know which kind it is,
  is to read it.

  What an app saw: `invariant(RCTActionSheetManager, "ActionSheetManager doesn't
  exist")`, thrown from inside React Native at the first call, since the module
  is a `get` and came back null. Measured by taking the module back out, which
  is what the end-to-end scenario's second sabotage does.

  **A popup menu rather than a dialog**, which `core/ActionSheet.h` argues:
  a dialog can show the sheet's `title` and `message` and cannot show eight
  choices without looking like a mistake, cannot grey one out, and cannot be
  dismissed with Escape. So the title and the message go in as disabled entries
  above a separator, and the options follow -- which is why the module carries
  an offset between a menu index and the index the app passed. Every index is a
  valid index, so that arithmetic is the quietest bug available here and it has
  its own tests on both sides.

  Three things fell out of it:

  - **`anchor` works, on all three, with no per-host code.** It is a react tag,
    and `UIManager::findShadowNodeByTag_DEPRECATED` plus
    `getRelativeLayoutMetrics` turn it into the view's frame in the window's own
    coordinates -- so the menu opens under the control that asked for it, which
    is what a desktop does and what iOS uses the anchor for on an iPad. The
    scenario asserts the resolved point rather than trusting it.
  - **A dismissal reports the app's `cancelButtonIndex`**, which iOS does not
    do: `UIAlertController` invokes the callback from a button's handler, so a
    popover tapped away calls nothing. Escape closing a menu is ordinary where
    that is not, and an app that named a cancel choice has already said what to
    do with it. With no cancel choice, nothing is reported, which is iOS's
    answer.
  - **`BASALT_TEST_MENU` could answer with a disabled entry**, and now cannot.
    The instrument already refused a separator, a submenu's parent and an entry
    naming a role this desktop lacks, on the principle that what a script can
    answer is what a person could have clicked; a greyed entry belongs in that
    list and was missing until an action sheet put its title into the menu as
    one. The guard is `scriptedMenuIndex` now, which is a function rather than
    part of `presentMenu` so that it can be asserted.

  What is left is `dismissActionSheet`, recorded rather than written: a popup
  menu on these three runs its own tracking loop -- `TrackPopupMenu` and
  `popUpMenuPositioningItem` both block the thread that opened it -- so a call
  arriving on the JavaScript thread cannot reach the menu while it is up. Taking
  it down needs each toolkit's own cancel called from inside that loop:
  `[NSMenu cancelTracking]`, `EndMenu()`, `gtk_popover_popdown`. React Native's
  own JavaScript guards the method with a `typeof` check, so iOS treats it as
  optional too; it logs what it could not do rather than pretending.

- **The twenty-one others nobody answers are not gaps**, and the audit says so
  per module rather than leaving a reader to wonder. Android's halves of
  cross-platform APIs (`IntentAndroid` for `Linking`, `DialogManagerAndroid` for
  `Alert`, `PermissionsAndroid`, `HeadlessJsTaskSupport`, `DeviceEventManager`
  for the back button) are not reached off Android, and the iOS halves of the
  same APIs *are* answered here. `ToastAndroid` is the exception that is
  answered rather than skipped: it is a `getEnforcing`, so an app that imports
  it dies at startup, and since 2026-09-11 a module here logs the message
  instead -- which is a poor toast and a better answer than a crash. React Native's own test
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
