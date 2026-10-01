#include "AppearanceModule.h"

#include "ColorScheme.h"

namespace basalt {

using facebook::jsi::Runtime;
using facebook::jsi::String;

DesktopAppearanceModule::DesktopAppearanceModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : NativeAppearanceCxxSpec(std::move(jsInvoker)) {
  // Subscribed for the module's whole life rather than when JavaScript adds a
  // listener. `Appearance.addChangeListener` is a JavaScript-side subscription
  // on an event emitter; this is the thing that feeds it, and it has to be
  // running before the first listener arrives or the first change is lost.
  observerToken_ = addColorSchemeObserver([this](ColorScheme scheme) {
    emitDeviceEvent("appearanceChanged",
                    [scheme](Runtime &rt, std::vector<facebook::jsi::Value> &args) {
                      // NativeAppearancePreferences: { colorScheme }.
                      auto preferences = facebook::jsi::Object(rt);
                      preferences.setProperty(
                          rt, "colorScheme", String::createFromUtf8(rt, colorSchemeName(scheme)));
                      args.emplace_back(rt, preferences);
                    });
  });
}

DesktopAppearanceModule::~DesktopAppearanceModule() {
  removeColorSchemeObserver(observerToken_);
}

String DesktopAppearanceModule::getColorScheme(Runtime &rt) {
  return String::createFromUtf8(rt, colorSchemeName(effectiveColorScheme()));
}

void DesktopAppearanceModule::setColorScheme(Runtime &rt, std::string colorScheme) {
  (void)rt;
  // ColorSchemeOverride is 'light' | 'dark' | 'auto' | 'unspecified', and the
  // last two both mean "stop overriding". Handling only one of them is the kind
  // of gap an app finds and a test does not, because which spelling reaches
  // here depends on what the app passed.
  if (colorScheme == "unspecified" || colorScheme == "auto") {
    clearColorSchemeOverride();
    return;
  }
  setColorSchemeOverride(colorScheme);
}

void DesktopAppearanceModule::addListener(Runtime &rt, std::string eventName) {
  (void)rt;
  (void)eventName;
}

void DesktopAppearanceModule::removeListeners(Runtime &rt, double count) {
  (void)rt;
  (void)count;
}

} // namespace basalt
