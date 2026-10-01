// Tests for <ScrollView> on Windows: the offset, the clamp, the wheel's
// routing, and the commands.
//
// The first tests any of the three desktops has for a scroll manager. GTK's and
// AppKit's are exercised end to end by `scripts/compare_hosts.sh` running
// `js/scroll.js` on both, which is a better test in every way except that it
// needs two working desktops and this machine has one -- WSL2 will not start
// here, so nothing else can check Windows against another host. So the
// arithmetic gets tested directly instead.
//
// What is observable from here is narrower than what the manager does, and the
// two limits are worth naming rather than working around. A ShadowView built by
// hand has no ShadowNodeFamily, so `State::updateState` finds nothing to commit
// into and the unthrottled state write-back cannot be asserted on. And an
// EventEmitter built by hand has no EventDispatcher, so `onScroll` goes
// nowhere. What is left is the part that decides where the content actually
// sits: the clamp, the adoption, the routing and the commands.

#include "TestHarness.h"

#include "Win32MountingManager.h"
#include "Win32ScrollView.h"
// For hitTestTag: what a press would find, which is the other half of "the
// scroll moved" and the half a scroll manager can get silently wrong.
#include "Win32TouchDispatcher.h"

#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>
#include <react/renderer/components/view/ViewProps.h>

#include <memory>

using basalt::Win32MountingManager;
using basalt::Win32ScrollViewManager;
using basalt::win32::RnWin32View;
using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::Point;
using facebook::react::Rect;
using facebook::react::ScrollViewProps;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ScrollViewState;
using facebook::react::ShadowNodeFamily;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::Size;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

ShadowView makeView(Tag tag, float x, float y, float width, float height) {
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = x, .y = y}, .size = {.width = width, .height = height}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = std::make_shared<ViewProps>();
  view.layoutMetrics = metrics;
  return view;
}

// A ScrollView whose viewport is `width`x`height` and whose content is
// `contentWidth`x`contentHeight` -- which is the whole of what the manager
// needs, because Yoga has already laid the content out at its full size by the
// time a mutation arrives.
ShadowView makeScrollView(Tag tag,
                          float x,
                          float y,
                          float width,
                          float height,
                          float contentWidth,
                          float contentHeight,
                          bool scrollEnabled = true,
                          Point initialOffset = Point{0, 0}) {
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = x, .y = y}, .size = {.width = width, .height = height}};

  auto props = std::make_shared<ScrollViewProps>();
  props->scrollEnabled = scrollEnabled;

  ScrollViewState data;
  data.contentOffset = initialOffset;
  data.contentBoundingRect = Rect{.origin = {.x = 0, .y = 0},
                                  .size = Size{.width = contentWidth, .height = contentHeight}};

  ShadowView view;
  view.componentName = "ScrollView";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  // An empty family: nothing can be committed back through it, which is what
  // makes the state write-back unobservable here. See the file header.
  view.state = std::make_shared<const ScrollViewShadowNode::ConcreteState>(
      std::make_shared<const ScrollViewState>(data), ShadowNodeFamily::Weak{});
  return view;
}

void apply(Win32MountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

// Creates and inserts one shadow view under `parent`, in a single transaction.
void mount(Win32MountingManager &manager, Tag parent, const ShadowView &shadowView) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(shadowView));
  mutations.push_back(ShadowViewMutation::InsertMutation(parent, shadowView, 0));
  apply(manager, std::move(mutations));
}

// One notch's worth of pixels, in the direction a contentOffset grows.
constexpr double kNotch = Win32ScrollViewManager::kWheelStepPixels;

} // namespace

// The offset is applied to the view, which is where painting and hit testing
// both read it -- so this is the one assertion that covers all three.
TEST(win32_a_wheel_scrolls_the_view_under_it) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));

  EXPECT(manager.scrollAt(root, 200, 150, 0, 5 * kNotch));
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 5 * kNotch, 0.01);
  EXPECT_NEAR(manager.viewForTag(10)->scrollX(), 0.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// The clamp, at both ends. Content 1000 tall in a 300 viewport can scroll 700
// and no further, and nothing can scroll above the top.
TEST(win32_the_offset_is_clamped_to_the_content) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));

  manager.scrollAt(root, 200, 150, 0, 100 * kNotch);
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 700.0, 0.01);

  manager.scrollAt(root, 200, 150, 0, -100 * kNotch);
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 0.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// Content that fits does not scroll at all, but the wheel is still consumed --
// matching AppKit, and matching what every desktop does: a list at its end must
// not hand the wheel to whatever is behind it.
TEST(win32_content_that_fits_consumes_the_wheel_and_does_not_move) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 200));

  EXPECT(manager.scrollAt(root, 200, 150, 0, 5 * kNotch));
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 0.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// scrollEnabled: false does not consume, so the wheel keeps going up the tree.
TEST(win32_a_disabled_scroll_view_passes_the_wheel_on) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000, false));

  EXPECT(!manager.scrollAt(root, 200, 150, 0, 5 * kNotch));
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 0.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// A wheel over a plain row scrolls the list containing it. GTK gets this from
// attaching a controller per widget and AppKit from the responder chain; here
// it is the parent walk in scrollAt, so it is worth an assertion.
TEST(win32_a_wheel_over_a_child_scrolls_its_scroll_view) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));
  mount(manager, 10, makeView(11, 0, 0, 400, 80));

  // The point lands on the row, not on the ScrollView.
  EXPECT_EQ(static_cast<long>(basalt::hitTestTag(root, 200, 40)), 11L);

  EXPECT(manager.scrollAt(root, 200, 40, 0, 2 * kNotch));
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 2 * kNotch, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// A list inside a list scrolls the inner one. Innermost-first is the whole
// reason the walk starts at the hit view rather than at the root.
TEST(win32_a_nested_scroll_view_takes_the_wheel_first) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));
  mount(manager, 10, makeScrollView(11, 0, 0, 400, 100, 400, 500));

  EXPECT(manager.scrollAt(root, 200, 50, 0, 1 * kNotch));
  EXPECT_NEAR(manager.viewForTag(11)->scrollY(), kNotch, 0.01);
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 0.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// Scrolling moves what a press lands on. `hitTest` adds the scroll offset back
// on the way down, so this needs nothing from the scroll manager -- which is
// exactly the claim being checked, because a list that scrolls correctly and
// answers presses meant for the row above is the bug this prevents.
TEST(win32_hit_testing_follows_the_scroll) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));
  // Two rows, the second below the fold at y=400.
  mount(manager, 10, makeView(12, 0, 400, 400, 100));
  mount(manager, 10, makeView(11, 0, 0, 400, 100));

  EXPECT_EQ(static_cast<long>(basalt::hitTestTag(root, 200, 50)), 11L);

  manager.scrollAt(root, 200, 150, 0, 400);
  EXPECT_EQ(static_cast<long>(basalt::hitTestTag(root, 200, 50)), 12L);

  manager.destroySurfaceRoot(kSurfaceId);
}

// A ScrollView must clip whatever its props say. Its content is deliberately
// larger than its frame, and without this it paints over its siblings.
TEST(win32_a_scroll_view_clips_regardless_of_overflow) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));

  EXPECT(manager.viewForTag(10)->clipsChildren());

  manager.destroySurfaceRoot(kSurfaceId);
}

// An offset already in the state is adopted rather than reset. That is what a
// `contentOffset` prop looks like by the time it reaches here, and what a
// surface that was suspended and remounted carries.
TEST(win32_an_initial_offset_in_the_state_is_adopted) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000, true, Point{0, 120}));

  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 120.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// Content can shrink under a scrolled offset -- a list whose items were
// filtered away. The re-clamp on every mutation is what stops the view from
// being left showing empty space below its own content.
TEST(win32_shrinking_content_pulls_the_offset_back) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));

  manager.scrollAt(root, 200, 150, 0, 700);
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 700.0, 0.01);

  ShadowViewMutationList mutations;
  mutations.push_back(
      ShadowViewMutation::UpdateMutation(makeScrollView(10, 0, 0, 400, 300, 400, 1000),
                                         makeScrollView(10, 0, 0, 400, 300, 400, 500),
                                         kSurfaceId));
  apply(manager, std::move(mutations));

  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 200.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// scrollTo and scrollToEnd, which arrive through dispatchCommand. js/scroll.js
// drives itself through a ref, so this is the path that file exercises and the
// only one that needs no wheel.
TEST(win32_scroll_to_and_scroll_to_end_move_the_offset) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));

  manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 265, false));
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 265.0, 0.01);

  manager.applyCommand(10, "scrollToEnd", folly::dynamic::array(false));
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 700.0, 0.01);

  // scrollTo past the end is clamped like any other offset.
  manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 5000, false));
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 700.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}

// A command for a tag that is not a ScrollView, and one for a tag that is gone.
// Both are ordinary: a scrollTo can arrive after the surface that owned it was
// torn down.
TEST(win32_a_command_for_an_unknown_tag_is_survivable) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeView(10, 0, 0, 100, 100));

  manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 100, false));
  manager.applyCommand(999, "scrollTo", folly::dynamic::array(0, 100, false));
  manager.applyCommand(10, "somethingElse", folly::dynamic::array());

  manager.destroySurfaceRoot(kSurfaceId);
}

// The entry has to go with the view. A wheel arriving after the ScrollView was
// deleted must find nothing rather than a dangling RnWin32View pointer.
TEST(win32_deleting_a_scroll_view_forgets_its_entry) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000));
  EXPECT(manager.scrollAt(root, 200, 150, 0, kNotch));

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::RemoveMutation(
      kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 400, 1000), 0));
  mutations.push_back(
      ShadowViewMutation::DeleteMutation(makeScrollView(10, 0, 0, 400, 300, 400, 1000)));
  apply(manager, std::move(mutations));

  EXPECT(manager.viewForTag(10) == nullptr);
  EXPECT(!manager.scrollAt(root, 200, 150, 0, kNotch));
  manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 100, false));

  manager.destroySurfaceRoot(kSurfaceId);
}

// Nothing under the pointer, and no surface at all. The host reads the return
// value to decide whether to leave the message to DefWindowProc, so "false"
// here is the difference between a wheel over a non-scrolling app doing nothing
// and doing nothing *and* being swallowed.
TEST(win32_a_wheel_over_nothing_is_not_consumed) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeView(10, 0, 0, 100, 100));

  EXPECT(!manager.scrollAt(root, 50, 50, 0, kNotch));
  EXPECT(!manager.scrollAt(root, 5000, 5000, 0, kNotch));
  EXPECT(!manager.scrollAt(nullptr, 50, 50, 0, kNotch));

  manager.destroySurfaceRoot(kSurfaceId);
}

// Horizontal, which travels the same path with the other axis and is easy to
// leave half-wired: WM_MOUSEHWHEEL is a separate message with the opposite
// sign convention to WM_MOUSEWHEEL.
TEST(win32_a_horizontal_wheel_scrolls_the_other_axis) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);
  root->setFrame(0, 0, 400, 300);
  mount(manager, kSurfaceId, makeScrollView(10, 0, 0, 400, 300, 1200, 300));

  EXPECT(manager.scrollAt(root, 200, 150, 3 * kNotch, 0));
  EXPECT_NEAR(manager.viewForTag(10)->scrollX(), 3 * kNotch, 0.01);
  EXPECT_NEAR(manager.viewForTag(10)->scrollY(), 0.0, 0.01);

  manager.destroySurfaceRoot(kSurfaceId);
}
