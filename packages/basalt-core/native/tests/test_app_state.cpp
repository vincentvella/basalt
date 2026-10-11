// What `AppState.currentState` says, which was the empty string on every host
// until 2026-10-10. See core/AppStateHost.h.

#include "TestHarness.h"

#include "AppStateHost.h"

#include <sstream>
#include <string>
#include <vector>

using basalt::AppStateKind;
using basalt::appStateFor;
using basalt::appStateName;

TEST(app_state_is_the_two_facts_a_desktop_has) {
  EXPECT(appStateFor(true, true) == AppStateKind::Active);
  // Visible and not frontmost: iOS's `inactive` is a transition, and the
  // desktop analogue is another window being in front -- which is the same
  // thing an app uses it for.
  EXPECT(appStateFor(false, true) == AppStateKind::Inactive);
  // Invisible wins over focused: a minimised window that still holds focus is
  // not something an app should treat as frontmost.
  EXPECT(appStateFor(true, false) == AppStateKind::Background);
  EXPECT(appStateFor(false, false) == AppStateKind::Background);
}

TEST(app_state_names_are_react_natives_own) {
  EXPECT(std::string(appStateName(AppStateKind::Active)) == "active");
  EXPECT(std::string(appStateName(AppStateKind::Inactive)) == "inactive");
  EXPECT(std::string(appStateName(AppStateKind::Background)) == "background");
}

// An application that has drawn a window is active, which is what an app
// reading `currentState` before any event arrives should be told. Upstream
// answers the empty string, which is not one of React Native's own states.
TEST(app_state_starts_active) {
  EXPECT(basalt::currentAppState() == AppStateKind::Active);
}

TEST(app_state_tells_the_listener_only_when_it_changes) {
  std::vector<AppStateKind> seen;
  basalt::setAppStateListener([&seen](AppStateKind state) { seen.push_back(state); });

  basalt::notifyAppState(AppStateKind::Active);  // already active
  basalt::notifyAppState(AppStateKind::Inactive);
  basalt::notifyAppState(AppStateKind::Inactive);
  basalt::notifyAppState(AppStateKind::Background);
  basalt::notifyAppState(AppStateKind::Active);

  EXPECT_EQ((long)seen.size(), 3L);
  EXPECT(seen[0] == AppStateKind::Inactive);
  EXPECT(seen[1] == AppStateKind::Background);
  EXPECT(seen[2] == AppStateKind::Active);

  // And the state is readable without a listener, which is where
  // `getCurrentAppState` reads it.
  EXPECT(basalt::currentAppState() == AppStateKind::Active);
  basalt::setAppStateListener(nullptr);
}
