// `Appearance`, which is how an app learns it should be dark.
//
// Everything here is portable. Which scheme the system is in is not, and is
// behind the seam in ColorScheme.h.
//
// React Native looks this module up with `getEnforcing`, so its absence is not
// a quiet no-op: `Appearance.getColorScheme()` throws and takes the first render
// with it. That is what a stock Expo app hit in phase 29.

#pragma once

#include <FBReactNativeSpec/FBReactNativeSpecJSI.h>

#include <string>

namespace basalt {

class DesktopAppearanceModule
    : public facebook::react::NativeAppearanceCxxSpec<DesktopAppearanceModule> {
 public:
  explicit DesktopAppearanceModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
  ~DesktopAppearanceModule() override;

  static constexpr const char *kModuleName = "Appearance";

  facebook::jsi::String getColorScheme(facebook::jsi::Runtime &rt);
  void setColorScheme(facebook::jsi::Runtime &rt, std::string colorScheme);

  // The listener bookkeeping React Native's NativeEventEmitter contract wants.
  // The real subscription is the one taken out in the constructor: JavaScript
  // adding a listener does not change what this module watches, only whether
  // anybody is there when it fires.
  void addListener(facebook::jsi::Runtime &rt, std::string eventName);
  void removeListeners(facebook::jsi::Runtime &rt, double count);

 private:
  int observerToken_{0};
};

} // namespace basalt
