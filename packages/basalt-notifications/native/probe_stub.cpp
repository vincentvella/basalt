// What this package owes a platform, stubbed, for core/portability_probe.cpp.
//
// The probe links `basalt_core` and nothing platform-specific, and answers one
// question: what does a second desktop have to implement? A capability package
// compiles its platform-independent half into that same library -- here,
// `ExpoNotificationModules.cpp` -- so the seam it calls is one more symbol the
// probe has to satisfy, and one more thing a new platform owes.
//
// This lives in the package rather than in the probe because the probe cannot
// depend on it. An app installs capability packages it wants and not the ones
// it does not, which is the whole point of the boundary in docs/DECISIONS.md;
// a probe that included this package's header unconditionally made core fail
// to compile in any app without the package. It did, and the failure read as a
// missing file in core rather than as a dependency that should not exist.
//
// So the contract is symmetrical with the rest: a package contributes core
// sources, include directories, an Expo installer, a per-host implementation,
// and -- if its seam is called from the half that lands in core -- a probe
// stub. See the package's CMakeLists for the properties that carry each.

#include "Notifications.h"

namespace basalt {

NotificationSupport notificationSupport() {
  return {false, "no platform in this build"};
}

bool showNotification(const std::string &, const NotificationContent &) {
  return false;
}

bool dismissNotification(const std::string &) {
  return false;
}

void dismissAllNotifications() {}

std::vector<std::string> presentedNotifications() {
  return {};
}

} // namespace basalt
