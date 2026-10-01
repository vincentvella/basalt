// `StatusBarManager`, which a desktop does not have.
//
// React Native's <StatusBar> resolves to this module, and `expo-status-bar`
// wraps it, so the component that a blank Expo app puts on screen by default
// reaches for it before rendering. It is looked up with getEnforcing, so its
// absence is not a quiet no-op -- it throws, and takes the app's first render
// with it.
//
// So the module exists and does nothing, which is the truthful implementation
// rather than a placeholder. There is no status bar on a desktop: its height is
// zero, and hiding or styling something with no pixels is a no-op in the
// strongest sense, not a stub waiting to be filled in.
//
// The Android spec is the one implemented, for the same reason the platform
// constants module answers the Android shape: this platform reports Android
// constants and shares Android's JavaScript. The iOS spec carries methods about
// network activity indicators that mean even less here.

#pragma once

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>

#include <optional>
#include <string>

namespace basalt {

class DesktopStatusBarModule
    : public facebook::react::NativeStatusBarManagerAndroidCxxSpec<DesktopStatusBarModule> {
 public:
  explicit DesktopStatusBarModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
      : NativeStatusBarManagerAndroidCxxSpec(std::move(jsInvoker)) {}

  facebook::jsi::Object getConstants(facebook::jsi::Runtime &rt);

  void setColor(facebook::jsi::Runtime &rt, double color, bool animated);
  void setTranslucent(facebook::jsi::Runtime &rt, bool translucent);
  void setStyle(facebook::jsi::Runtime &rt, std::optional<std::string> statusBarStyle);
  void setHidden(facebook::jsi::Runtime &rt, bool hidden);
};

} // namespace basalt
