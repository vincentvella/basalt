// The core React Native modules ReactCxxPlatform does not provide.
//
// Upstream supplies nineteen, from two providers rather than one:
// ReactCxxTurboModuleProvider has AppState, DeviceInfo, DevLoadingView,
// ExceptionsManager, ImageLoader, LogBox, NativeAnimatedModule, Networking,
// PlatformConstants and WebSocketModule, and DefaultTurboModules -- which it
// falls through to -- has the web APIs. `scripts/audit_modules.py` prints the
// list per module and per host; this comment said fifteen from one provider
// until that audit read both chains. What is here is the set a real app reaches
// for and finds missing, and it splits in two ways that matter.
//
// **How they fail.** `Clipboard` and `Vibration` are looked up with
// `getEnforcing`, so their absence *throws at import* -- an app that so much as
// imports Clipboard dies at startup with no render. The rest are looked up with
// `get`, which returns null, so React Native's JavaScript falls back or quietly
// does nothing: `Alert.alert()` was a dialog that never appeared and
// `Linking.openURL()` a link that never opened, with no error either way. The
// second kind is worse to debug and was less obviously urgent, which is why
// this file does both at once.
//
// **What they need.** Six modules, three of which touch an operating system --
// clipboard, opening a URL, showing a dialog -- and three of which answer
// rather than act. The first three go through `PlatformServices.h`; the other
// three are portable in full.
//
// One file because they are a set and each is small. Splitting them would mean
// six headers saying very little and one seam between them.

#pragma once

#include "PlatformServices.h"
#include "SettingsStore.h"

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>

#include <optional>
#include <string>

namespace basalt {

// Throws at import without this: `Clipboard` is a getEnforcing lookup.
class DesktopClipboardModule
    : public facebook::react::NativeClipboardCxxSpec<DesktopClipboardModule> {
 public:
  explicit DesktopClipboardModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeClipboardCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "Clipboard";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  facebook::jsi::Value getString(facebook::jsi::Runtime &rt);
  void setString(facebook::jsi::Runtime &rt, facebook::jsi::String content);
};

// Also a getEnforcing lookup, and meaningless on a desktop -- so the module
// exists and does nothing, which is the truthful implementation rather than a
// placeholder. A machine with no vibration motor cannot be made to buzz by
// anything this file could contain.
class DesktopVibrationModule
    : public facebook::react::NativeVibrationCxxSpec<DesktopVibrationModule> {
 public:
  explicit DesktopVibrationModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeVibrationCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "Vibration";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  void vibrate(facebook::jsi::Runtime &rt, double pattern);
  void vibrateByPattern(facebook::jsi::Runtime &rt,
                        facebook::jsi::Array pattern,
                        double repeat);
  void cancel(facebook::jsi::Runtime &rt);
};

// `Alert.alert()`, which until now did nothing at all -- a confirmation dialog
// that never appeared and never reported a choice, so the code after it simply
// never ran.
class DesktopAlertModule
    : public facebook::react::NativeAlertManagerCxxSpec<DesktopAlertModule> {
 public:
  explicit DesktopAlertModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeAlertManagerCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "AlertManager";

  void alertWithArgs(facebook::jsi::Runtime &rt,
                     facebook::jsi::Object args,
                     facebook::jsi::Function callback);
};

// `Linking.openURL()`, which until now opened nothing.
// The URL an application was launched with, for `Linking.getInitialURL()`.
//
// A desktop hands one over on the command line -- a `.desktop` entry's `%u`, a
// registered scheme on macOS, a shell association on Windows -- so a host
// records whatever argument looked like one and this answers with it. Set
// before the surface starts, which is before any JavaScript can ask.
//
// What this is not is a URL delivered to an application that is *already*
// running. That needs single-instance activation on each desktop -- a
// GApplication with G_APPLICATION_HANDLES_OPEN, an Apple Event handler, a named
// pipe -- and is a separate piece of work; see docs/BACKLOG.md.
void setInitialUrl(const std::string &url);
const std::string &initialUrl();

class DesktopLinkingModule
    : public facebook::react::NativeLinkingManagerCxxSpec<DesktopLinkingModule> {
 public:
  explicit DesktopLinkingModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeLinkingManagerCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "LinkingManager";

  facebook::jsi::Value getInitialURL(facebook::jsi::Runtime &rt);
  facebook::jsi::Value canOpenURL(facebook::jsi::Runtime &rt, facebook::jsi::String url);
  facebook::jsi::Value openURL(facebook::jsi::Runtime &rt, facebook::jsi::String url);
  facebook::jsi::Value openSettings(facebook::jsi::Runtime &rt);
  void addListener(facebook::jsi::Runtime &rt, facebook::jsi::String eventName);
  void removeListeners(facebook::jsi::Runtime &rt, double count);
};

// `Share.share()`.
//
// The last of React Native's core modules that was throwing, and the one whose
// JavaScript had to be replaced as well: `Share.js` branches on `Platform.OS`
// being exactly `android` or `ios` and rejects with "Unsupported platform"
// otherwise, so the module was never reached at all. See
// src/overrides/Share.js.
//
// Shaped like Android's `ShareModule` rather than iOS's action sheet, because
// the Android one is the promise-shaped API and the iOS one is a callback pair
// on `ActionSheetManager`. It takes what Android's does plus a `url`, which
// React Native marks as iOS-only and a desktop has no reason to refuse.
//
// The promise settles when the picker does, which may be a long time: this is
// the first asynchronous thing on this platform that is genuinely asynchronous
// underneath, so it is the first to need a promise that is not already settled.
// `AsyncPromise` is ReactCommon's, and it handles the thread hop back.
class DesktopShareModule : public facebook::react::NativeShareModuleCxxSpec<DesktopShareModule> {
 public:
  explicit DesktopShareModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeShareModuleCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "ShareModule";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  facebook::jsi::Value share(facebook::jsi::Runtime &rt,
                             facebook::jsi::Object content,
                             std::optional<facebook::jsi::String> dialogTitle);
};

// `Settings`, which an app reaches through `Settings.get` and `Settings.set`.
//
// The second module whose JavaScript had to be replaced as well, and found the
// same way: `Settings.js` branches on `Platform.OS === 'ios'` and hands
// everything else `SettingsFallback`, which warns and answers null. So the
// module was never reached, and the audit's claim that this one *throws* was
// wrong in the app's favour -- see src/overrides/Settings.ts and
// docs/backlog/modules.md.
//
// A file rather than each desktop's own settings store, for the reasons
// core/SettingsStore.h argues. The store is a member rather than a global: it
// is read in the constructor and written on every change, and a reload builds a
// new module, which re-reads the file -- which is the state.
//
// No `settingsUpdated` event, deliberately. iOS sets `_ignoringUpdates` around
// its own `setValues` for the reason this has nothing to emit: `Settings.set`
// has already merged into JavaScript's copy by the time the module is called,
// so `_sendObservations` would see no change and call no watcher. What does
// fire a watcher is a change from outside the process, which means watching the
// file; that is recorded rather than written.
class DesktopSettingsManagerModule
    : public facebook::react::NativeSettingsManagerCxxSpec<DesktopSettingsManagerModule> {
 public:
  explicit DesktopSettingsManagerModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

  static constexpr const char *kModuleName = "SettingsManager";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  void setValues(facebook::jsi::Runtime &rt, facebook::jsi::Object values);
  void deleteValues(facebook::jsi::Runtime &rt, facebook::jsi::Array values);

 private:
  SettingsStore store_;
};

// Right-to-left layout. Portable in full: Yoga already does the work, and this
// is the switch that turns it on.
class DesktopI18nManagerModule
    : public facebook::react::NativeI18nManagerCxxSpec<DesktopI18nManagerModule> {
 public:
  explicit DesktopI18nManagerModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeI18nManagerCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "I18nManager";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  void allowRTL(facebook::jsi::Runtime &rt, bool allowRTL);
  void forceRTL(facebook::jsi::Runtime &rt, bool forceRTL);
  void swapLeftAndRightInRTL(facebook::jsi::Runtime &rt, bool flipStyles);
};

// What assistive technology is doing, as *iOS* names it.
//
// The Android spec, `AccessibilityInfo`, is implemented below as well, and this
// is the one that gets used. `AccessibilityInfo.js` branches on
// `Platform.OS === 'android'` in eight places and a third platform takes the
// else branch every time -- which is the iOS path, calling
// `NativeAccessibilityManagerIOS`. So implementing only the Android module left
// `isScreenReaderEnabled()` throwing "NativeAccessibilityManagerIOS is not
// available", which is how this was found.
//
// The same shape as `Image`'s view config in phase 26 and `TextInput`'s render
// in phase 09: React Native branching two ways on a platform that is neither,
// and landing on iOS. Worth expecting rather than rediscovering.
//
// Each getter takes (onSuccess, onError) rather than returning.
class DesktopAccessibilityManagerModule
    : public facebook::react::NativeAccessibilityManagerCxxSpec<DesktopAccessibilityManagerModule> {
 public:
  explicit DesktopAccessibilityManagerModule(
      std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeAccessibilityManagerCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "AccessibilityManager";

  void getCurrentBoldTextState(facebook::jsi::Runtime &rt,
                               facebook::jsi::Function onSuccess,
                               facebook::jsi::Function onError);
  void getCurrentGrayscaleState(facebook::jsi::Runtime &rt,
                                facebook::jsi::Function onSuccess,
                                facebook::jsi::Function onError);
  void getCurrentInvertColorsState(facebook::jsi::Runtime &rt,
                                   facebook::jsi::Function onSuccess,
                                   facebook::jsi::Function onError);
  void getCurrentReduceMotionState(facebook::jsi::Runtime &rt,
                                   facebook::jsi::Function onSuccess,
                                   facebook::jsi::Function onError);
  void getCurrentDarkerSystemColorsState(facebook::jsi::Runtime &rt,
                                         facebook::jsi::Function onSuccess,
                                         facebook::jsi::Function onError);
  void getCurrentPrefersCrossFadeTransitionsState(facebook::jsi::Runtime &rt,
                                                  facebook::jsi::Function onSuccess,
                                                  facebook::jsi::Function onError);
  void getCurrentReduceTransparencyState(facebook::jsi::Runtime &rt,
                                         facebook::jsi::Function onSuccess,
                                         facebook::jsi::Function onError);
  void getCurrentVoiceOverState(facebook::jsi::Runtime &rt,
                                facebook::jsi::Function onSuccess,
                                facebook::jsi::Function onError);

  void setAccessibilityContentSizeMultipliers(
      facebook::jsi::Runtime &rt,
      facebook::jsi::Object jsMultipliers);
  void setAccessibilityFocus(facebook::jsi::Runtime &rt, double reactTag);
  void announceForAccessibility(facebook::jsi::Runtime &rt, facebook::jsi::String announcement);
  void announceForAccessibilityWithOptions(facebook::jsi::Runtime &rt,
                                           facebook::jsi::String announcement,
                                           facebook::jsi::Object options);
};

// The Android spelling of the same thing. Kept because an app can reach it
// directly and because which one React Native picks is a JavaScript decision
// that could change.
class DesktopAccessibilityInfoModule
    : public facebook::react::NativeAccessibilityInfoCxxSpec<DesktopAccessibilityInfoModule> {
 public:
  explicit DesktopAccessibilityInfoModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeAccessibilityInfoCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "AccessibilityInfo";

  void isReduceMotionEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isInvertColorsEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isHighTextContrastEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isTouchExplorationEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isAccessibilityServiceEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void isGrayscaleEnabled(facebook::jsi::Runtime &rt, facebook::jsi::Function onSuccess);
  void setAccessibilityFocus(facebook::jsi::Runtime &rt, double reactTag);
  void announceForAccessibility(facebook::jsi::Runtime &rt, facebook::jsi::String announcement);
  void getRecommendedTimeoutMillis(facebook::jsi::Runtime &rt,
                                   double mSec,
                                   facebook::jsi::Function onSuccess);
};

// `ToastAndroid`, which is a getEnforcing lookup and so throws at import.
//
// Android's toast is a transient message that appears over the app and fades.
// A desktop has no such thing built in: the nearest equivalent is a system
// notification, which is a different thing with a different lifetime and needs
// a notification API neither platform has wired up.
//
// So the message is logged rather than shown. That is not a good answer and it
// is a better one than throwing: an app that shows a toast is telling the user
// something, and losing it silently is worse than losing it visibly in a log.
// See docs/BACKLOG.md.
class DesktopToastModule
    : public facebook::react::NativeToastAndroidCxxSpec<DesktopToastModule> {
 public:
  explicit DesktopToastModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeToastAndroidCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "ToastAndroid";

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);
  void show(facebook::jsi::Runtime &rt, facebook::jsi::String message, double duration);
  void showWithGravity(facebook::jsi::Runtime &rt,
                       facebook::jsi::String message,
                       double duration,
                       double gravity);
  void showWithGravityAndOffset(facebook::jsi::Runtime &rt,
                                facebook::jsi::String message,
                                double duration,
                                double gravity,
                                double xOffset,
                                double yOffset);
};

// `DevSettings`, which is a getEnforcing lookup -- and which upstream provides
// *only when a dev server exists*.
//
// So in a release bundle it is absent and `DevSettings.reload()` throws, which
// is a production crash reachable from ordinary code. This module exists to
// fill that gap and must therefore be offered only when upstream's is not:
// the host passes `enableDevMode` and skips this in dev, where the real one can
// actually reload.
class DesktopDevSettingsModule
    : public facebook::react::NativeDevSettingsCxxSpec<DesktopDevSettingsModule> {
 public:
  explicit DesktopDevSettingsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeDevSettingsCxxSpec(std::move(jsInvoker)) {}

  static constexpr const char *kModuleName = "DevSettings";

  void reload(facebook::jsi::Runtime &rt);
  void reloadWithReason(facebook::jsi::Runtime &rt, facebook::jsi::String reason);
  void onFastRefresh(facebook::jsi::Runtime &rt);
  void setHotLoadingEnabled(facebook::jsi::Runtime &rt, bool isHotLoadingEnabled);
  void setProfilingEnabled(facebook::jsi::Runtime &rt, bool isProfilingEnabled);
  void toggleElementInspector(facebook::jsi::Runtime &rt);
  void addMenuItem(facebook::jsi::Runtime &rt, facebook::jsi::String title);
  void openDebugger(facebook::jsi::Runtime &rt);
  void addListener(facebook::jsi::Runtime &rt, facebook::jsi::String eventName);
  void removeListeners(facebook::jsi::Runtime &rt, double count);
  void setIsShakeToShowDevMenuEnabled(facebook::jsi::Runtime &rt, bool enabled);
};

} // namespace basalt
