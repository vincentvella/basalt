#include "StatusBarModule.h"

namespace basalt {

using facebook::jsi::Object;
using facebook::jsi::Runtime;

Object DesktopStatusBarModule::getConstants(Runtime &rt) {
  Object constants(rt);
  // Zero, and meant literally. React Native's own JavaScript lays out around
  // this value, so reporting a plausible-looking phone height would push every
  // app's content down by a status bar that is not there.
  constants.setProperty(rt, "HEIGHT", 0.0);
  constants.setProperty(rt, "DEFAULT_BACKGROUND_COLOR", 0.0);
  return constants;
}

void DesktopStatusBarModule::setColor(Runtime & /*rt*/, double /*color*/, bool /*animated*/) {}
void DesktopStatusBarModule::setTranslucent(Runtime & /*rt*/, bool /*translucent*/) {}
void DesktopStatusBarModule::setStyle(Runtime & /*rt*/, std::optional<std::string> /*style*/) {}
void DesktopStatusBarModule::setHidden(Runtime & /*rt*/, bool /*hidden*/) {}

} // namespace basalt
