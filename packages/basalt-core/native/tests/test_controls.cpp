// The four components that are a control rather than a box, minus the pixels.
//
// What is asserted here is the half that three hosts share: how a ShadowView's
// props become a ControlState, what the tree dump says about it, and the pull
// gesture that fires a <RefreshControl>. None of it needs a toolkit, which is
// the point -- a GtkSwitch, an NSSwitch and a Direct2D rounded rectangle have
// nothing in common, and everything above them has to be identical or the same
// app is a different app on each desktop.
//
// The pull is the part most worth a test. It has exactly one rule that is easy
// to get wrong and invisible when it is -- fire once per gesture, not once per
// wheel notch -- and a mouse cannot be asked about it.

#include "TestHarness.h"

#include <memory>

#include "DesktopControls.h"
#include "PullToRefresh.h"

#include <react/renderer/components/FBReactNativeSpec/Props.h>

using basalt::ControlKind;
using basalt::ControlState;
using basalt::controlKindFor;
using basalt::controlStateOf;
using basalt::describeControl;
using basalt::kPullToRefreshThreshold;
using basalt::PullToRefreshTracker;
using facebook::react::ShadowView;

namespace {

// A mounted view of a given component name, carrying props built by the caller.
// Fabric's ShadowView is a plain struct, so this is the whole fixture.
//
// The props arrive already in a shared_ptr rather than being copied into one.
// BaseViewProps has a deleted copy constructor at React Native 0.86 -- which
// is what a current Expo app installs -- so copying compiled here, against
// the checkout's newer React Native, and failed in the one place it matters.
template <typename Props>
ShadowView viewOf(const char *componentName, std::shared_ptr<Props> props) {
  ShadowView view;
  view.componentName = componentName;
  view.tag = 10;
  view.props = std::move(props);
  return view;
}

} // namespace

TEST(controls_name_decides_kind) {
  EXPECT(controlKindFor("ActivityIndicatorView") == ControlKind::ActivityIndicator);
  EXPECT(controlKindFor("Switch") == ControlKind::Switch);
  EXPECT(controlKindFor("PullToRefreshView") == ControlKind::PullToRefresh);
  // Everything else, including the modal -- which is a control in the backlog's
  // sense and not in this one: it has no widget, it is an overlay.
  EXPECT(controlKindFor("View") == ControlKind::None);
  EXPECT(controlKindFor("ModalHostView") == ControlKind::None);
  EXPECT(controlKindFor(nullptr) == ControlKind::None);
}

TEST(controls_indicator_props_are_read) {
  auto props = std::make_shared<facebook::react::ActivityIndicatorViewProps>();
  props->animating = true;
  props->size = facebook::react::ActivityIndicatorViewSize::Large;

  const ControlState state = controlStateOf(viewOf("ActivityIndicatorView", props));
  EXPECT(state.kind == ControlKind::ActivityIndicator);
  EXPECT(state.on);
  EXPECT(state.large);
  // React Native's default, which is the one that makes a stopped indicator
  // draw nothing at all rather than draw a still one.
  EXPECT(state.hidesWhenStopped);
  EXPECT(!state.hasForeground);
}

TEST(controls_switch_props_are_read) {
  auto props = std::make_shared<facebook::react::SwitchProps>();
  props->value = true;
  props->disabled = true;

  const ControlState state = controlStateOf(viewOf("Switch", props));
  EXPECT(state.kind == ControlKind::Switch);
  EXPECT(state.on);
  EXPECT(state.disabled);
}

TEST(controls_refresh_props_are_read) {
  auto props = std::make_shared<facebook::react::PullToRefreshViewProps>();
  props->refreshing = true;

  const ControlState state = controlStateOf(viewOf("PullToRefreshView", props));
  EXPECT(state.kind == ControlKind::PullToRefresh);
  EXPECT(state.on);
}

TEST(controls_a_plain_view_is_not_a_control) {
  auto props = std::make_shared<facebook::react::ViewProps>();
  const ControlState state = controlStateOf(viewOf("View", props));
  EXPECT(state.kind == ControlKind::None);
  EXPECT(describeControl(state).empty());
}

TEST(controls_description_is_one_string_for_three_hosts) {
  ControlState state;
  state.kind = ControlKind::Switch;
  state.on = true;
  EXPECT_EQ(describeControl(state), std::string("switch:on"));
  state.disabled = true;
  EXPECT_EQ(describeControl(state), std::string("switch:on:disabled"));

  state = ControlState{};
  state.kind = ControlKind::ActivityIndicator;
  state.on = true;
  EXPECT_EQ(describeControl(state), std::string("spinner:animating"));
  state.large = true;
  EXPECT_EQ(describeControl(state), std::string("spinner-large:animating"));
  state.on = false;
  EXPECT_EQ(describeControl(state), std::string("spinner-large:stopped"));

  state = ControlState{};
  state.kind = ControlKind::PullToRefresh;
  EXPECT_EQ(describeControl(state), std::string("refresh:idle"));
}

// --- The pull ----------------------------------------------------------------

TEST(pull_a_scroll_view_with_no_refresh_control_never_fires) {
  PullToRefreshTracker tracker;
  EXPECT(!tracker.pull(1, 1000.0));
  EXPECT_EQ((long)tracker.controlFor(1), 0L);
}

TEST(pull_fires_once_the_threshold_is_crossed) {
  PullToRefreshTracker tracker;
  tracker.attach(1, 2);
  EXPECT_EQ((long)tracker.controlFor(1), 2L);

  EXPECT(!tracker.pull(1, kPullToRefreshThreshold / 2.0));
  EXPECT(tracker.pull(1, kPullToRefreshThreshold / 2.0 + 1.0));
}

TEST(pull_fires_once_per_gesture_and_not_once_per_notch) {
  PullToRefreshTracker tracker;
  tracker.attach(1, 2);

  EXPECT(tracker.pull(1, kPullToRefreshThreshold + 1.0));
  // The wheel is still turning. React Native's contract is one onRefresh per
  // pull, and without the latch this is sixty a second.
  EXPECT(!tracker.pull(1, 100.0));
  EXPECT(!tracker.pull(1, 100.0));

  // Let go, and it can fire again.
  tracker.release(1);
  EXPECT(!tracker.pull(1, 10.0));
  EXPECT(tracker.pull(1, kPullToRefreshThreshold));
}

TEST(pull_a_scroll_away_from_the_top_rearms_it) {
  PullToRefreshTracker tracker;
  tracker.attach(1, 2);
  EXPECT(!tracker.pull(1, kPullToRefreshThreshold - 1.0));
  // The list moved, so this was not one continuous pull past the top.
  tracker.release(1);
  EXPECT(!tracker.pull(1, kPullToRefreshThreshold - 1.0));
}

TEST(pull_forgetting_either_end_drops_the_pair) {
  PullToRefreshTracker tracker;

  // The control went away -- an app that stopped passing `refreshControl`.
  tracker.attach(1, 2);
  tracker.forget(2);
  EXPECT_EQ((long)tracker.controlFor(1), 0L);
  EXPECT(!tracker.pull(1, 1000.0));

  // The scroll view went away, which is the ordinary unmount.
  tracker.attach(3, 4);
  tracker.forget(3);
  EXPECT_EQ((long)tracker.controlFor(3), 0L);
  // And the reverse map went with it, so re-attaching 4 elsewhere is clean.
  tracker.attach(5, 4);
  EXPECT_EQ((long)tracker.controlFor(5), 4L);
}
