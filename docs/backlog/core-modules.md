# Core modules this platform does not provide

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (6):**

1. React Native's JavaScript branches two ways and a third platform lands on iOS
2. Notifications on macOS need a person
3. Notification *delivery* back to the app
4. Scheduling
5. Packaging the host as an application
6. A Metro error still has no red box
7. ~~`ToastAndroid` is absent, deliberately~~

React Native answers nineteen of the modules it asks for, from *two* providers
rather than one, and this file said fourteen from one until the native-module
audit read both chains. `ReactCxxTurboModuleProvider` has `AppState`,
`DeviceInfo`, `DevLoadingView`, `ExceptionsManager`, `ImageLoader`, `LogBox`,
`NativeAnimatedModule`, `Networking`, `PlatformConstants` and `WebSocketModule`;
`DefaultTurboModules`, which it falls through to, has the web APIs --
`NativeDOMCxx`, `NativeIdleCallbacksCxx`, `NativeIntersectionObserverCxx`,
`NativeMicrotasksCxx`, `NativeMutationObserverCxx`, `NativePerformanceCxx`,
`NativeReactNativeFeatureFlagsCxx`, `NativeViewTransitionCxx` and
`ReactDevToolsRuntimeSettingsModule`. Three of those are answered only under a
condition the chain does not state -- a dev UI delegate, a feature flag that is
off by default, a debug macro -- which is a thing a running host said and the
code did not. `scripts/audit_modules.py` is where that list now comes from, per
module and per host, rather than from a paragraph; see [modules.md](modules.md).
`DevSettings` and `SourceCode` were on this list and are answered here instead:
upstream provides `DevSettings` only when there is a dev server, so a release
bundle had `DevSettings.reload()` throwing.

Everything below is a React Native API with no implementation anywhere in this
stack, and each one is ours to write against GTK, GLib or the portals.

How each fails matters, and splits in two. Most are looked up with
`TurboModuleRegistry.get`, which returns null, so React Native's JavaScript
falls back or silently does nothing; the API appears to work and simply has no
effect. `Clipboard` and `Vibration` use `getEnforcing`, which throws at import,
so anything importing them dies at startup.

- ~~**`Appearance`**~~ **done in phase 30**, on both desktops. What is left of
  it: live system switching is subscribed to and has never been watched firing,
  there is no `AppearanceProvider` or per-view override, and nothing native
  repaints on the switch because neither platform has a `PlatformColor` or a
  system control to repaint.
- ~~**`BlobModule`**~~ **done in phase 31**, on both desktops. What is left:
  blob request bodies and `responseType: 'blob'`, both blocked upstream (below),
  and `readAsArrayBuffer`, which does not exist here and does not exist in React
  Native's own JavaScript either; `FileReader` has no such method, so there is
  no native half to be missing yet. Worth knowing before somebody goes looking.
  binary websocket frames, and `readAsText` understanding only UTF-8.
- ~~**`Clipboard`**, **`Vibration`**, **`AlertManager`**, **`LinkingManager`**,
  **`I18nManager`**, **`AccessibilityInfo`**~~ **done in phase 32**, on both
  desktops. What is left: alert prompts on Linux (GtkAlertDialog has no text
  field), `login-password` alerts on either, incoming URLs, reading another
  application's clipboard on Linux, and `setAccessibilityFocus` /
  `announceForAccessibility`, which need a per-view accessibility handle neither
  mounting manager exposes.
- **React Native's JavaScript branches two ways and a third platform lands on
  iOS.** `TextInput.js` (phase 09), `ImageViewNativeComponent`'s view config
  (phase 26) and `AccessibilityInfo.js` (phase 32) all did this, each found by an
  app failing rather than by review. Worth checking for deliberately the next
  time a module misbehaves.

  Six instances now, and the last three do not land on iOS -- they land on a
  warning or on nothing, which is harder to notice. `Share.js` rejects with
  "Unsupported platform", `Alert.js` has no `else` at all, and `Settings.js`
  hands everything that is not iOS a `SettingsFallback` that warns and answers
  null. In each case a native module existed, or was written, and was never
  reached; each needed a file in `src/overrides` as well. **The test for this is
  cheap and nobody ran it for years: read the JavaScript of the API before
  implementing the module under it.**
- **Notifications on macOS need a person.** The implementation is real:
  `UNUserNotificationCenter`, behind the bundle check that keeps an unbundled
  host from raising `bundleProxyForCurrentProcess is nil`, and the first send
  asks for authorisation. Whether a banner appears then depends on someone
  granting it, which needs a real launch and a real prompt; an automated run
  gets "Notifications are not allowed for this application" and cannot do
  anything about it. What *is* asserted is that a bundled host answers
  `granted` where an unbundled one answers `denied`, and that the send is
  accepted.
- ~~**`ToastAndroid` is absent**~~, and it stopped being absent on 2026-09-11,
  a month before this entry was read again: `DesktopToastModule` answers it on
  all three hosts and logs the message. The entry said the throw was a known
  answer, and a `getEnforcing` lookup nobody answers takes the app down at
  import, which is not an answer -- a toast an app shows is it telling somebody
  something, and losing it to a log is better than losing the process. Found by
  the native-module audit, which reads the provider chains rather than this
  file; see [modules.md](modules.md).
- ~~**`ShareModule`**~~ is done on all three, and it took a JavaScript override
  as well as a module: `Share.js` branches on `Platform.OS` being exactly
  `android` or `ios` and rejects with "Unsupported platform" otherwise, so no
  desktop module was ever reached. macOS shows `NSSharingServicePicker`. Linux
  and Windows show the picker core/ShareFallback.h builds from a clipboard and a
  mail client, which is the honest answer where no share service exists, see
  that header for the argument. Windows' own `DataTransferManager` share UI is
  the upgrade, and needs WinRT interop that cannot be tested from a Mac.

  Three things fell out of doing it, all of them bugs that were already there:
  `Alert.alert()` did nothing at all on any desktop (below); `showAlert` on
  macOS read its request through a dangling reference, which showed as a dialog
  with no text; and no instrument could get past a modal dialog, which is now
  `BASALT_TEST_DIALOG`.
- ~~**`Alert.alert()` did nothing.**~~ Not a gap, a silent break, in a module
  the backlog recorded as finished in phase 32. Two of them in one chain:
  `Alert.js` branches on `Platform.OS` being exactly `ios` or `android` with no
  else, and `RCTAlertManager.js` is one of the self-importing shims, so it
  resolved to its `.android.js` sibling, which calls `DialogManagerAndroid` (  a module this platform does not have) and returns. `AlertManager` also
  answered with Android's three-argument callback rather than the two its own
  spec declares. Every one of those was invisible because nothing ever reached
  the next layer. e2e/alert.tsx logged "showing the alert" and asserted only that
  the main queue kept running, which it does whether or not a dialog appears.
- ~~**A desktop notification API.**~~ There is one, and it is not this project's:
  the contract implemented is `expo-notifications`, which is what an app
  reaching for notifications is most likely already using, the same argument
  react-native-gesture-handler and expo-clipboard were ported on. Thirteen
  native modules, three of them with methods; the other ten exist because
  `requireNativeModule` throws on a name it cannot find and the package asks for
  every one at import.

  Linux can show one, through `org.freedesktop.Notifications` on the session bus,
   not `g_application_send_notification`, which routes through the portal and
  displays nothing without an installed `.desktop` file matching the
  application id. macOS and Windows report `denied` with a reason, because both
  need the host to be an installed, bundled application:
  `UNUserNotificationCenter` does not merely fail for a process with no bundle
  identifier, it raises and terminates, and a Windows toast wants an
  AppUserModelID and a Start Menu shortcut. `getPermissionsAsync` answering
  `denied` is how the API itself says this, and is what expo's documentation
  tells an app to check.

  The send is verified, and the end-to-end suite verifies it on every run that
  has `dbus-daemon`: it starts a session bus and
  `basalt_notification_stub` on it: a stand-in daemon that owns the name,
  answers `Notify` and prints what it was asked to show: then asserts that the
  permission is granted, that the service received the notification, and that it
  carried the app's own words. Not `dbus-run-session`, which on macOS insists on
  launchd's socket and will not start a plain bus. Where there is no
  `dbus-daemon` the suite asserts the other half instead: that the host reports
  why it cannot send.
- **Notification *delivery* back to the app.** `ExpoNotificationsEmitter` is
  registered and empty, so an app never hears that a notification was tapped.
  The freedesktop specification has `ActionInvoked` and `NotificationClosed`
  signals for exactly this and they are a subscription away, but the thing on
  the other end (expo's handler and response machinery) is a larger surface
  than presenting one.
- **Scheduling.** `scheduleNotificationAsync` delivers immediately, which is
  what a null trigger means, and rejects by name for anything else. A real
  trigger needs a timer that outlives the process and somewhere to keep the
  queue, which is a feature rather than a branch.
- **Packaging the host as an application**, an `.app` on macOS, a Start Menu
  shortcut with an AppUserModelID on Windows. It is the thing standing between
  this platform and notifications on two of three desktops, and it is not only
  notifications: the Dock icon, the menu bar name, file associations and a
  registered URL scheme all come with the same bundle, and so does
  `Linking.getInitialURL` for a URL delivered to an app that is already running.
- ~~**LogBox has no red box.**~~ It has one, on all three hosts. What was
  missing was not an overlay: `LogBoxInspectorContainer` is registered by
  AppRegistry under the name "LogBox" exactly as an app registers its own
  component, ReactCxxPlatform already implements the `LogBox` TurboModule, and
  it only provides it when a host hands `ReactHost` a `SurfaceDelegate`. This
  project passed null, so `NativeLogBox.show()` was a call into nothing, and
  the *toasts* worked all along, because AppContainer renders those inside the
  app's own surface.
- **A Metro error still has no red box.** Phase 33 stopped the error page being
  compiled as JavaScript and prints Metro's own message, which is most of the
  value, but the host exits rather than showing it. An app already running when
  a reload fails is a separate case, and it currently keeps running the code it
  has, with the error only in the log. Now that a second surface exists this is
  much closer than it was.

- ~~**`useNativeDriver: true` throws.**~~ "Native animated module is not
  available" was every native-driven `Animated` call, and the fix was the same
  shape as LogBox's: ReactCommon has a C++ implementation of the whole animated
  graph (`react/renderer/animated`), ReactCxxPlatform provides the module for
  it, and it only does so when a host hands over a
  `NativeAnimatedNodesManagerProvider`. Found because LogBox's own spinner uses
  one.
