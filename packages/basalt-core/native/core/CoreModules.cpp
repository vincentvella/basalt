#include "CoreModules.h"

#include "ShareFallback.h"
#include "TestDialog.h"
#include "UIManagerAccess.h"

#include <jsi/JSIDynamic.h>
#include <react/bridging/Promise.h>
#include <react/renderer/core/LayoutableShadowNode.h>

#include <glog/logging.h>

#include <mutex>

namespace basalt {

using facebook::jsi::Array;
using facebook::jsi::Function;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;

namespace {

// A promise already settled. These answers are all in memory or one system call
// away, so there is nothing to wait for -- and going through JavaScript's own
// Promise.resolve needs no invoker and no thread hop.
Value resolved(Runtime &rt, Value &&value) {
  auto promise = rt.global().getPropertyAsObject(rt, "Promise");
  return promise.getPropertyAsFunction(rt, "resolve").callWithThis(rt, promise, value);
}

Value rejected(Runtime &rt, const std::string &message) {
  auto promise = rt.global().getPropertyAsObject(rt, "Promise");
  auto error = rt.global()
                   .getPropertyAsFunction(rt, "Error")
                   .callAsConstructor(rt, String::createFromUtf8(rt, message));
  return promise.getPropertyAsFunction(rt, "reject").callWithThis(rt, promise, error);
}

std::string optionalString(Runtime &rt, const Object &object, const char *key) {
  const Value value = object.getProperty(rt, key);
  return value.isString() ? value.asString(rt).utf8(rt) : std::string{};
}

} // namespace

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

Object DesktopClipboardModule::getConstants(Runtime &rt) {
  return Object(rt);
}

Value DesktopClipboardModule::getString(Runtime &rt) {
  return resolved(rt, Value(String::createFromUtf8(rt, clipboardText())));
}

void DesktopClipboardModule::setString(Runtime &rt, String content) {
  setClipboardText(content.utf8(rt));
}

// ---------------------------------------------------------------------------
// Vibration
// ---------------------------------------------------------------------------

Object DesktopVibrationModule::getConstants(Runtime &rt) {
  return Object(rt);
}

void DesktopVibrationModule::vibrate(Runtime &rt, double pattern) {
  (void)rt;
  (void)pattern;
}

void DesktopVibrationModule::vibrateByPattern(Runtime &rt, Array pattern, double repeat) {
  (void)rt;
  (void)pattern;
  (void)repeat;
}

void DesktopVibrationModule::cancel(Runtime &rt) {
  (void)rt;
}

// ---------------------------------------------------------------------------
// Alert
// ---------------------------------------------------------------------------

void DesktopAlertModule::alertWithArgs(Runtime &rt, Object args, Function callback) {
  AlertRequest request;
  request.title = optionalString(rt, args, "title");
  request.message = optionalString(rt, args, "message");

  // `buttons` is [{text}, ...] in the order Alert.alert was given them, and the
  // callback reports an index into that same list.
  const Value buttons = args.getProperty(rt, "buttons");
  if (buttons.isObject() && buttons.asObject(rt).isArray(rt)) {
    Array list = buttons.asObject(rt).asArray(rt);
    const size_t count = list.size(rt);
    for (size_t i = 0; i < count; i++) {
      Object button = list.getValueAtIndex(rt, i).asObject(rt);
      std::string text = optionalString(rt, button, "text");
      if (text.empty()) {
        text = "OK";
      }
      request.buttons.push_back(std::move(text));
    }
  }
  if (request.buttons.empty()) {
    // What Alert.alert() with no buttons means on every platform.
    request.buttons.emplace_back("OK");
  }

  // `type` is 'default' | 'plain-text' | 'secure-text' | 'login-password'. Only
  // a single text field is offered here: a login-password prompt is two fields
  // and a layout, and answering it with one would silently lose the password.
  const std::string type = optionalString(rt, args, "type");
  if (type == "plain-text" || type == "secure-text") {
    request.hasTextInput = true;
    request.defaultText = optionalString(rt, args, "defaultValue");
    request.placeholder = optionalString(rt, args, "keyboardType");
  } else if (!type.empty() && type != "default") {
    LOG(WARNING) << "Alert type '" << type << "' is not supported; showing a plain alert";
  }

  // The callback has to survive until a human presses something, and it belongs
  // to the runtime. A shared_ptr captured by the platform's completion handler
  // is what keeps it alive without keeping the runtime alive.
  auto shared = std::make_shared<Function>(std::move(callback));
  auto invoker = jsInvoker_;

  presentAlert(request, [shared, invoker](int buttonIndex, const std::string &text) {
    if (invoker == nullptr) {
      return;
    }
    // Back onto the JavaScript thread: the platform calls this from its own
    // main thread, and a jsi::Function may only be called on the runtime's.
    invoker->invokeAsync([shared, buttonIndex, text](Runtime &rt) {
      // `(id, value)`, which is what NativeAlertManager's own spec declares --
      // two arguments, not three. The three-argument form belongs to Android's
      // DialogManagerAndroid, which is a different module. This sent that one
      // for three phases and nothing noticed, because `Alert.alert()` never
      // reached the module at all: it branches on Platform.OS being exactly ios
      // or android and returns having done nothing otherwise. See
      // src/overrides/Alert.js.
      shared->call(rt, Value(buttonIndex), String::createFromUtf8(rt, text));
    });
  });
}

// ---------------------------------------------------------------------------
// Linking
// ---------------------------------------------------------------------------

namespace {

// Written once by the host before the surface starts, read from the JavaScript
// thread afterwards. A process-wide string rather than something threaded
// through the module's constructor, because a TurboModule is built by a factory
// that takes only a CallInvoker.
std::string &initialUrlStorage() {
  static std::string url;
  return url;
}

} // namespace

void setInitialUrl(const std::string &url) {
  initialUrlStorage() = url;
}

const std::string &initialUrl() {
  return initialUrlStorage();
}

Value DesktopLinkingModule::getInitialURL(Runtime &rt) {
  const std::string &url = initialUrl();
  // Null rather than an empty string when there was none, which is what React
  // Native's JavaScript checks for.
  if (url.empty()) {
    return resolved(rt, Value::null());
  }
  return resolved(rt, String::createFromUtf8(rt, url));
}

Value DesktopLinkingModule::canOpenURL(Runtime &rt, String url) {
  return resolved(rt, Value(canOpenUrl(url.utf8(rt))));
}

Value DesktopLinkingModule::openURL(Runtime &rt, String url) {
  const std::string target = url.utf8(rt);
  if (!openUrl(target)) {
    // Rejecting rather than resolving false: that is React Native's contract,
    // and an app that awaits openURL expects a throw when it fails.
    return rejected(rt, "Unable to open URL: " + target);
  }
  return resolved(rt, Value(true));
}

Value DesktopLinkingModule::openSettings(Runtime &rt) {
  // There is no per-application settings page on a desktop to open.
  return rejected(rt, "openSettings is not supported on this platform");
}

void DesktopLinkingModule::addListener(Runtime &rt, String eventName) {
  (void)rt;
  (void)eventName;
  // Incoming URLs would arrive here. Nothing delivers them yet; see
  // getInitialURL above.
}

void DesktopLinkingModule::removeListeners(Runtime &rt, double count) {
  (void)rt;
  (void)count;
}

// ---------------------------------------------------------------------------
// I18nManager
// ---------------------------------------------------------------------------

namespace {

struct I18nState {
  std::mutex mutex;
  bool allowRTL{false};
  bool forceRTL{false};
  bool swapLeftAndRight{true};
};

I18nState &i18n() {
  static I18nState state;
  return state;
}

} // namespace

Object DesktopI18nManagerModule::getConstants(Runtime &rt) {
  I18nState &state = i18n();
  const std::lock_guard<std::mutex> lock(state.mutex);

  Object constants(rt);
  // `isRTL` is what the layout actually is. Only `forceRTL` can make it true
  // here: deciding it from the system locale would need a locale database this
  // does not have, and guessing wrong flips an app's entire layout.
  constants.setProperty(rt, "isRTL", state.forceRTL);
  constants.setProperty(rt, "doLeftAndRightSwapInRTL", state.swapLeftAndRight);
  // Empty rather than a guess. React Native's JavaScript treats it as opaque
  // and only apps read it.
  constants.setProperty(rt, "localeIdentifier", String::createFromUtf8(rt, ""));
  return constants;
}

void DesktopI18nManagerModule::allowRTL(Runtime &rt, bool allow) {
  (void)rt;
  I18nState &state = i18n();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.allowRTL = allow;
}

void DesktopI18nManagerModule::forceRTL(Runtime &rt, bool force) {
  (void)rt;
  I18nState &state = i18n();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.forceRTL = force;
  // Deliberately not restarting or relaying out. On iOS and Android this takes
  // effect after a reload, and React Native's own documentation says so -- an
  // app that flipped direction mid-session would be a surprise rather than a
  // feature.
}

void DesktopI18nManagerModule::swapLeftAndRightInRTL(Runtime &rt, bool flipStyles) {
  (void)rt;
  I18nState &state = i18n();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.swapLeftAndRight = flipStyles;
}

// ---------------------------------------------------------------------------
// AccessibilityManager -- the iOS spelling, and the one that gets used
// ---------------------------------------------------------------------------

namespace {

// Every getter is (onSuccess, onError) and every answer here is false, for the
// reason given at the Android module below.
void answerState(Runtime &rt, Function &onSuccess, bool value) {
  onSuccess.call(rt, Value(value));
}

} // namespace

void DesktopAccessibilityManagerModule::getCurrentBoldTextState(Runtime &rt,
                                                                Function onSuccess,
                                                                Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentGrayscaleState(Runtime &rt,
                                                                 Function onSuccess,
                                                                 Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentInvertColorsState(Runtime &rt,
                                                                    Function onSuccess,
                                                                    Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentReduceMotionState(Runtime &rt,
                                                                    Function onSuccess,
                                                                    Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentDarkerSystemColorsState(Runtime &rt,
                                                                          Function onSuccess,
                                                                          Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentPrefersCrossFadeTransitionsState(
    Runtime &rt,
    Function onSuccess,
    Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentReduceTransparencyState(Runtime &rt,
                                                                          Function onSuccess,
                                                                          Function onError) {
  (void)onError;
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::getCurrentVoiceOverState(Runtime &rt,
                                                                 Function onSuccess,
                                                                 Function onError) {
  (void)onError;
  // Whether a screen reader is running. False rather than asking, for the same
  // reason as the rest: saying yes wrongly makes React Native's own components
  // announce things and alter focus order for a user who is not there.
  answerState(rt, onSuccess, false);
}

void DesktopAccessibilityManagerModule::setAccessibilityContentSizeMultipliers(
    Runtime &rt,
    Object jsMultipliers) {
  (void)rt;
  (void)jsMultipliers;
  // Dynamic Type's scale factors. Nothing here scales text for accessibility
  // settings, which is also why the text layer passes fontSizeMultiplier as 1.
}

void DesktopAccessibilityManagerModule::setAccessibilityFocus(Runtime &rt, double reactTag) {
  (void)rt;
  (void)reactTag;
}

void DesktopAccessibilityManagerModule::announceForAccessibility(Runtime &rt,
                                                                 String announcement) {
  (void)rt;
  (void)announcement;
}

void DesktopAccessibilityManagerModule::announceForAccessibilityWithOptions(Runtime &rt,
                                                                            String announcement,
                                                                            Object options) {
  (void)rt;
  (void)announcement;
  (void)options;
}

// ---------------------------------------------------------------------------
// AccessibilityInfo -- the Android spelling
// ---------------------------------------------------------------------------

namespace {

void answer(Runtime &rt, Function &onSuccess, bool value) {
  onSuccess.call(rt, Value(value));
}

} // namespace

// Every one of these is false, and each is false for a reason rather than as a
// placeholder.
//
// Neither desktop exposes "is a screen reader running" without asking the
// accessibility bus -- AT-SPI on Linux, and on macOS a check Apple has
// deprecated twice -- and answering *true* wrongly is worse than answering
// false: it makes React Native's own components change behaviour, announce
// things and alter focus order for a user who is not there.
void DesktopAccessibilityInfoModule::isReduceMotionEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isInvertColorsEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isHighTextContrastEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isTouchExplorationEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isAccessibilityServiceEnabled(Runtime &rt,
                                                                   Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::isGrayscaleEnabled(Runtime &rt, Function onSuccess) {
  answer(rt, onSuccess, false);
}

void DesktopAccessibilityInfoModule::setAccessibilityFocus(Runtime &rt, double reactTag) {
  (void)rt;
  (void)reactTag;
  // Moving a screen reader's cursor needs the platform's accessibility API to
  // post a focus notification for a specific view, which neither mounting
  // manager exposes by tag yet. See docs/BACKLOG.md.
}

void DesktopAccessibilityInfoModule::announceForAccessibility(Runtime &rt, String announcement) {
  (void)rt;
  (void)announcement;
  // Same gap: an announcement is a notification posted to the accessibility
  // bus, and nothing here has a handle on it.
}

void DesktopAccessibilityInfoModule::getRecommendedTimeoutMillis(Runtime &rt,
                                                                 double mSec,
                                                                 Function onSuccess) {
  // The original timeout, unmodified. This exists so an app can be told to give
  // a user longer; nothing here knows of a reason to.
  onSuccess.call(rt, Value(mSec));
}

// ---------------------------------------------------------------------------
// ToastAndroid
// ---------------------------------------------------------------------------

Object DesktopToastModule::getConstants(Runtime &rt) {
  Object constants(rt);
  // The values React Native's JavaScript passes back in. They have to exist and
  // be distinct; nothing here reads them.
  constants.setProperty(rt, "SHORT", 0.0);
  constants.setProperty(rt, "LONG", 1.0);
  constants.setProperty(rt, "TOP", 48.0);
  constants.setProperty(rt, "BOTTOM", 80.0);
  constants.setProperty(rt, "CENTER", 17.0);
  return constants;
}

void DesktopToastModule::show(Runtime &rt, String message, double duration) {
  (void)duration;
  LOG(INFO) << "toast: " << message.utf8(rt);
}

void DesktopToastModule::showWithGravity(Runtime &rt,
                                         String message,
                                         double duration,
                                         double gravity) {
  (void)duration;
  (void)gravity;
  LOG(INFO) << "toast: " << message.utf8(rt);
}

void DesktopToastModule::showWithGravityAndOffset(Runtime &rt,
                                                  String message,
                                                  double duration,
                                                  double gravity,
                                                  double xOffset,
                                                  double yOffset) {
  (void)duration;
  (void)gravity;
  (void)xOffset;
  (void)yOffset;
  LOG(INFO) << "toast: " << message.utf8(rt);
}

// ---------------------------------------------------------------------------
// DevSettings, for release bundles only
// ---------------------------------------------------------------------------
//
// Every method here does nothing, and that is what these mean without a dev
// server: there is nothing to reload from, no inspector to toggle, no debugger
// to open, and no dev menu to add an item to. In dev mode the host does not
// offer this module at all and upstream's real one is used instead.

void DesktopDevSettingsModule::reload(Runtime &rt) {
  (void)rt;
  LOG(WARNING) << "DevSettings.reload() has nothing to reload from in a release bundle";
}

void DesktopDevSettingsModule::reloadWithReason(Runtime &rt, String reason) {
  LOG(WARNING) << "DevSettings.reload() ignored (" << reason.utf8(rt) << ")";
}

void DesktopDevSettingsModule::onFastRefresh(Runtime &rt) {
  (void)rt;
}

void DesktopDevSettingsModule::setHotLoadingEnabled(Runtime &rt, bool isHotLoadingEnabled) {
  (void)rt;
  (void)isHotLoadingEnabled;
}

void DesktopDevSettingsModule::setProfilingEnabled(Runtime &rt, bool isProfilingEnabled) {
  (void)rt;
  (void)isProfilingEnabled;
}

void DesktopDevSettingsModule::toggleElementInspector(Runtime &rt) {
  (void)rt;
}

void DesktopDevSettingsModule::addMenuItem(Runtime &rt, String title) {
  (void)rt;
  (void)title;
}

void DesktopDevSettingsModule::openDebugger(Runtime &rt) {
  (void)rt;
}

void DesktopDevSettingsModule::addListener(Runtime &rt, String eventName) {
  (void)rt;
  (void)eventName;
}

void DesktopDevSettingsModule::removeListeners(Runtime &rt, double count) {
  (void)rt;
  (void)count;
}

void DesktopDevSettingsModule::setIsShakeToShowDevMenuEnabled(Runtime &rt, bool enabled) {
  (void)rt;
  (void)enabled;
}

// ---------------------------------------------------------------------------
// Share
// ---------------------------------------------------------------------------

Object DesktopShareModule::getConstants(Runtime &rt) {
  return Object(rt);
}

Value DesktopShareModule::share(Runtime &rt, Object content, std::optional<String> dialogTitle) {
  ShareRequest request;
  request.message = optionalString(rt, content, "message");
  request.url = optionalString(rt, content, "url");
  request.title = optionalString(rt, content, "title");
  if (dialogTitle.has_value()) {
    request.dialogTitle = dialogTitle->utf8(rt);
  }

  // Held by the callback, which is what keeps the promise alive until the
  // picker is done with it.
  auto promise = std::make_shared<facebook::react::AsyncPromise<folly::dynamic>>(rt, jsInvoker_);

  shareContent(request, [promise](ShareOutcome outcome, const std::string &message) {
    switch (outcome) {
      case ShareOutcome::Shared:
        // The shape Android's ShareModule resolves with. `activityType` is
        // iOS's and React Native's JavaScript fills in null for it.
        promise->resolve(folly::dynamic::object("action", "sharedAction"));
        return;
      case ShareOutcome::Dismissed:
        promise->resolve(folly::dynamic::object("action", "dismissedAction"));
        return;
      case ShareOutcome::Failed:
        promise->reject(facebook::react::Error(
            message.empty() ? std::string("sharing failed") : message));
        return;
    }
  });

  return Value(rt, facebook::react::bridging::toJs(rt, *promise, jsInvoker_));
}

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------

DesktopSettingsManagerModule::DesktopSettingsManagerModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : NativeSettingsManagerCxxSpec(std::move(jsInvoker)), store_(settingsFilePath()) {}

Object DesktopSettingsManagerModule::getConstants(Runtime &rt) {
  // `Settings._settings` is this object, once, at import. Everything
  // `Settings.get` answers afterwards is read out of it in JavaScript, so a
  // run that reads a setting never calls this module again -- which is why
  // reading is as cheap here as it is on iOS.
  Object constants(rt);
  constants.setProperty(rt, "settings", facebook::jsi::valueFromDynamic(rt, store_.values()));
  return constants;
}

void DesktopSettingsManagerModule::setValues(Runtime &rt, Object values) {
  store_.merge(facebook::jsi::dynamicFromValue(rt, Value(rt, values)));
}

void DesktopSettingsManagerModule::deleteValues(Runtime &rt, Array values) {
  const size_t count = values.size(rt);
  std::vector<std::string> keys;
  keys.reserve(count);
  for (size_t index = 0; index < count; ++index) {
    const Value element = values.getValueAtIndex(rt, index);
    // `Settings` has no deleteValues of its own: this is reached by an app
    // calling the module directly, so what arrives is whatever it passed.
    if (element.isString()) {
      keys.push_back(element.getString(rt).utf8(rt));
    }
  }
  store_.erase(keys);
}

// ---------------------------------------------------------------------------
// Action sheets
// ---------------------------------------------------------------------------

namespace {

// `anchor`, which iOS uses to hang the popover off a view and a desktop uses to
// put the menu under the control that opened it. A react tag, resolved through
// the shadow tree, so all three hosts answer it with no code of their own.
//
// Nothing when there is no UIManager, when the tag names no view, or when that
// view has no layout yet -- and the caller then shows the menu at the pointer,
// which is where a menu with no anchor goes.
std::optional<std::pair<double, double>> anchorPoint(int tag) {
  const std::shared_ptr<facebook::react::UIManager> uiManager = sharedUIManager();
  if (uiManager == nullptr) {
    return std::nullopt;
  }
  // Deprecated upstream and the only way to get from a tag to a node: every
  // other entry point takes a shadow node, and what JavaScript has is a tag.
  const std::shared_ptr<const facebook::react::ShadowNode> node =
      uiManager->findShadowNodeByTag_DEPRECATED(tag);
  if (node == nullptr) {
    return std::nullopt;
  }

  // Relative to the root, which is the window's own coordinates -- the same
  // space `showContextMenu` is passed from JavaScript.
  const facebook::react::LayoutableShadowNode::LayoutInspectingPolicy policy;
  const facebook::react::LayoutMetrics metrics =
      uiManager->getRelativeLayoutMetrics(*node, nullptr, policy);
  if (metrics == facebook::react::EmptyLayoutMetrics) {
    return std::nullopt;
  }

  // Under the anchor rather than over it, which is where a desktop puts the
  // menu a button opens. iOS points a popover at the view from whichever side
  // has room, which is an iPad's problem and not one a window has.
  return std::make_pair(static_cast<double>(metrics.frame.origin.x),
                        static_cast<double>(metrics.frame.origin.y + metrics.frame.size.height));
}

std::vector<int> indexList(Runtime &rt, const Object &options, const char *key) {
  std::vector<int> indices;
  const Value value = options.getProperty(rt, key);
  if (!value.isObject() || !value.asObject(rt).isArray(rt)) {
    return indices;
  }
  Array list = value.asObject(rt).asArray(rt);
  const size_t count = list.size(rt);
  for (size_t index = 0; index < count; ++index) {
    const Value element = list.getValueAtIndex(rt, index);
    if (element.isNumber()) {
      indices.push_back(static_cast<int>(element.asNumber()));
    }
  }
  return indices;
}

} // namespace

Object DesktopActionSheetModule::getConstants(Runtime &rt) {
  return Object(rt);
}

void DesktopActionSheetModule::showActionSheetWithOptions(Runtime &rt,
                                                          Object options,
                                                          Function callback) {
  ActionSheetRequest request;
  request.title = optionalString(rt, options, "title");
  request.message = optionalString(rt, options, "message");

  const Value titles = options.getProperty(rt, "options");
  if (titles.isObject() && titles.asObject(rt).isArray(rt)) {
    Array list = titles.asObject(rt).asArray(rt);
    const size_t count = list.size(rt);
    for (size_t index = 0; index < count; ++index) {
      const Value element = list.getValueAtIndex(rt, index);
      if (element.isString()) {
        request.options.push_back(element.getString(rt).utf8(rt));
      }
    }
  }
  if (request.options.empty()) {
    // `options` is required and React Native does not check it. Nothing to show
    // and no index to report, so the callback is not called -- which is what
    // iOS does with a sheet nobody pressed anything on.
    LOG(WARNING) << "ActionSheetIOS.showActionSheetWithOptions was given no options";
    return;
  }

  request.disabledIndices = indexList(rt, options, "disabledButtonIndices");
  const Value cancel = options.getProperty(rt, "cancelButtonIndex");
  if (cancel.isNumber()) {
    request.cancelIndex = static_cast<int>(cancel.asNumber());
  }

  const Value anchor = options.getProperty(rt, "anchor");
  if (anchor.isNumber()) {
    const std::optional<std::pair<double, double>> point =
        anchorPoint(static_cast<int>(anchor.asNumber()));
    if (point.has_value()) {
      request.x = point->first;
      request.y = point->second;
    }
  }

  const ActionSheetMenu menu = actionSheetMenu(request);
  // What it is about to show, because the presentation is the one part an
  // automated run skips: with BASALT_TEST_MENU set no menu appears, and this
  // line is where the anchor having been resolved is visible at all.
  LOG(INFO) << "action sheet: " << request.options.size() << " options at (" << menu.menu.x << ", "
            << menu.menu.y << ")";

  // The callback has to outlive the menu, which is up until somebody chooses.
  auto shared = std::make_shared<Function>(std::move(callback));
  auto invoker = jsInvoker_;

  presentMenu(menu.menu, [shared, invoker, request, menu](int chosen) {
    const std::optional<int> option = actionSheetChoice(request, menu, chosen);
    if (!option.has_value() || invoker == nullptr) {
      return;
    }
    // Back onto the JavaScript thread: a menu answers from the thread that drew
    // it, and a jsi::Function may only be called on the runtime's.
    invoker->invokeAsync(
        [shared, index = *option](Runtime &runtime) { shared->call(runtime, Value(index)); });
  });
}

void DesktopActionSheetModule::showShareActionSheetWithOptions(Runtime &rt,
                                                               Object options,
                                                               Function failureCallback,
                                                               Function successCallback) {
  ShareRequest request;
  request.message = optionalString(rt, options, "message");
  request.url = optionalString(rt, options, "url");
  request.title = optionalString(rt, options, "subject");

  if (request.message.empty() && request.url.empty()) {
    // What iOS does: an error in the log and neither callback called. Worth
    // keeping rather than improving, because an app that handled "no url or
    // message" by waiting forever is already written that way.
    LOG(ERROR) << "ActionSheetIOS.showShareActionSheetWithOptions had no `url` or `message`";
    return;
  }

  auto failure = std::make_shared<Function>(std::move(failureCallback));
  auto success = std::make_shared<Function>(std::move(successCallback));
  auto invoker = jsInvoker_;

  shareContent(request, [failure, success, invoker](ShareOutcome outcome,
                                                    const std::string &message) {
    if (invoker == nullptr) {
      return;
    }
    if (outcome == ShareOutcome::Failed) {
      invoker->invokeAsync([failure, message](Runtime &runtime) {
        // The shape `ShareActionSheetError` declares, which is what
        // RCTJSErrorFromNSError produces on iOS: a domain, a code and a
        // message. There is no NSError here, so the domain says where it came
        // from and the code says what happened.
        Object error(runtime);
        error.setProperty(runtime, "domain", String::createFromUtf8(runtime, "basalt.share"));
        error.setProperty(runtime, "code", String::createFromUtf8(runtime, "share-failed"));
        error.setProperty(
            runtime,
            "message",
            String::createFromUtf8(runtime, message.empty() ? "sharing failed" : message));
        failure->call(runtime, error);
      });
      return;
    }
    // `(completed, method)`. iOS reports the activity type as the method and
    // null when the sheet was dismissed; a desktop picker has no activity type
    // to report, which is the same null `Share.share()` resolves with here.
    const bool completed = outcome == ShareOutcome::Shared;
    invoker->invokeAsync([success, completed](Runtime &runtime) {
      success->call(runtime, Value(completed), Value::null());
    });
  });
}

void DesktopActionSheetModule::dismissActionSheet(Runtime &rt) {
  (void)rt;
  // Nothing to dismiss, and not for want of trying. A popup menu on these three
  // runs its own tracking loop -- `TrackPopupMenu` on Win32 and
  // `popUpMenuPositioningItem` on AppKit both block the thread that opened it
  // until the menu goes away -- so this call, which arrives on the JavaScript
  // thread, cannot reach the menu while it is up: the UI thread is inside the
  // menu and the work this would post runs after it has closed. Taking it down
  // needs each toolkit's cancel called from inside its own loop
  // (`[NSMenu cancelTracking]`, `EndMenu()`, `gtk_popover_popdown`), which is a
  // per-host piece of work; see docs/backlog/modules.md.
  //
  // React Native's own JavaScript guards this one -- `typeof
  // RCTActionSheetManager.dismissActionSheet === 'function'` -- so iOS treats it
  // as optional too. It is here because the generated spec declares it, and it
  // says what it did rather than pretending.
  LOG(WARNING) << "ActionSheetIOS.dismissActionSheet: a popup menu here closes on Escape or on a "
                  "choice and cannot be taken down from JavaScript";
}

} // namespace basalt
