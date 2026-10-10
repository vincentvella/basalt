// Tests for the fling GTK hands over half-finished.
//
// `<ScrollView>` itself is exercised end to end by `scripts/compare_hosts.sh`
// running `e2e/scroll.js` against both hosts, which is a better test than
// anything here -- it compares one desktop's answer to another's. Momentum is
// the part that cannot reach: a real fling needs a touchscreen, and the frame
// clock that drives it needs a mapped window and two seconds of main loop.
//
// So the tick is driven directly instead. `fling` and `advanceFling` enter
// exactly where GDK's `decelerate` signal and the frame clock do and skip
// nothing above them; what goes untested is that GTK emits `decelerate` at all,
// which is the same gap every synthesised input in this project leaves.
//
// The manager is built standalone rather than through GtkMountingManager: it
// takes an emitter lookup and nothing else, and a lookup that answers nothing
// is honest here -- an EventEmitter built by hand has no EventDispatcher, so
// `onMomentumScrollBegin` would go nowhere anyway. What is observable is where
// the content actually sits, which is what a fling is for.

#include "TestHarness.h"

#include "GtkMountingManager.h"
#include "GtkScrollView.h"
#include "RnView.h"

#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>

#include <memory>
#include <optional>
#include <string>
#include <sstream>

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
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;
using facebook::react::Size;
using facebook::react::SurfaceId;
using facebook::react::Tag;

namespace {

constexpr SurfaceId kSurfaceId = 1;
constexpr double kFrame = 1.0 / 60.0;

ShadowView makeScrollView(Tag tag,
                          float width,
                          float height,
                          float contentWidth,
                          float contentHeight,
                          float decelerationRate = 0,
                          bool pagingEnabled = false,
                          Point contentOffset = Point{0, 0},
                          bool centerContent = false,
                          facebook::react::ScrollViewIndicatorStyle indicatorStyle =
                              facebook::react::ScrollViewIndicatorStyle::Default,
                          std::optional<facebook::react::ScrollViewMaintainVisibleContentPosition>
                              maintainVisible = std::nullopt) {
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = width, .height = height}};

  auto props = std::make_shared<ScrollViewProps>();
  props->decelerationRate = decelerationRate;
  props->pagingEnabled = pagingEnabled;
  props->contentOffset = contentOffset;
  props->centerContent = centerContent;
  props->indicatorStyle = indicatorStyle;
  props->maintainVisibleContentPosition = maintainVisible;

  ScrollViewState data;
  // Seeded from the prop, which is what ScrollViewShadowNode does for real:
  // `initialStateData` returns the props' own `contentOffset`. So a test that
  // set only the prop would not be testing the path an app takes.
  data.contentOffset = contentOffset;
  data.contentBoundingRect = Rect{.origin = {.x = 0, .y = 0},
                                  .size = Size{.width = contentWidth, .height = contentHeight}};

  ShadowView view;
  view.componentName = "ScrollView";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  // An empty family: nothing can be committed back through it, so the
  // unthrottled state write-back is not observable from here.
  view.state = std::make_shared<const ScrollViewShadowNode::ConcreteState>(
      std::make_shared<const ScrollViewState>(data), ShadowNodeFamily::Weak{});
  return view;
}

// A manager with one ScrollView registered, and the widget it is registered
// against. The widget is owned by the caller's scope, which is why the tests
// sink and unref it rather than parenting it into anything.
struct Scroller {
  basalt::GtkScrollViewManager manager;
  RnView *view;
  Tag tag;

  Scroller(Tag tag,
           float contentHeight,
           float decelerationRate = 0,
           bool pagingEnabled = false,
           Point contentOffset = Point{0, 0},
           bool centerContent = false,
           facebook::react::ScrollViewIndicatorStyle indicatorStyle =
               facebook::react::ScrollViewIndicatorStyle::Default)
      : manager([](Tag) { return facebook::react::EventEmitter::Shared{}; }),
        view(rn_view_new(static_cast<int>(tag))),
        tag(tag) {
    g_object_ref_sink(view);
    rn_view_set_frame(view, 0, 0, 400, 300);
    manager.update(view,
                   makeScrollView(tag,
                                  400,
                                  300,
                                  400,
                                  contentHeight,
                                  decelerationRate,
                                  pagingEnabled,
                                  contentOffset,
                                  centerContent,
                                  indicatorStyle));
  }

  ~Scroller() {
    manager.remove(tag);
    g_object_unref(view);
  }

  Scroller(const Scroller &) = delete;
  Scroller &operator=(const Scroller &) = delete;

  double offsetY() const {
    double x = 0;
    double y = 0;
    rn_view_get_scroll_offset(view, &x, &y);
    return y;
  }

  // Runs the fling to a standstill and returns how many frames it took.
  // Bounded, so a fling that never stops fails the assertion rather than
  // hanging the suite.
  int settle() {
    int frames = 0;
    while (manager.advanceFling(tag, kFrame) && frames < 6000) {
      frames++;
    }
    return frames + 1;
  }
};

} // namespace

TEST(scrollview_a_fling_coasts_and_stops) {
  Scroller scroller(10, 4000);
  EXPECT(scroller.manager.fling(scroller.tag, 0, 1200));
  const int frames = scroller.settle();

  // The same flight core/ScrollMomentum.h is tested for, arriving as an offset.
  EXPECT(scroller.offsetY() > 450);
  EXPECT(scroller.offsetY() < 750);
  EXPECT(frames > 60);
  EXPECT(frames < 200);
  // And it is over: another tick moves nothing.
  const double settled = scroller.offsetY();
  EXPECT(!scroller.manager.advanceFling(scroller.tag, kFrame));
  EXPECT_NEAR(scroller.offsetY(), settled, 0.001);
}

TEST(scrollview_a_fling_stops_at_the_end_of_the_content) {
  // 300pt of viewport over 500pt of content: 200pt to travel, and a fling with
  // enough velocity to want far more.
  Scroller scroller(11, 500);
  EXPECT(scroller.manager.fling(scroller.tag, 0, 4000));
  const int frames = scroller.settle();

  EXPECT_NEAR(scroller.offsetY(), 200.0, 0.001);
  // The assertion that matters: it noticed. A fling that hits an edge still has
  // velocity left, and without the check it would coast against the stop for a
  // second before reporting that it had finished -- during which a list would
  // believe it was still scrolling.
  EXPECT(frames < 30);
}

TEST(scrollview_a_fling_is_clamped_at_the_top) {
  Scroller scroller(12, 4000);
  // Already at the top, flung further up: there is nowhere to go.
  EXPECT(scroller.manager.fling(scroller.tag, 0, -1500));
  scroller.settle();
  EXPECT_NEAR(scroller.offsetY(), 0.0, 0.001);
}

TEST(scrollview_a_slow_flick_starts_no_fling) {
  Scroller scroller(13, 4000);
  // Below the model's stopping speed. Reporting a momentum scroll that began
  // and ended in the same frame is worse than reporting none.
  EXPECT(!scroller.manager.fling(scroller.tag, 0, 5));
  EXPECT(!scroller.manager.advanceFling(scroller.tag, kFrame));
  EXPECT_NEAR(scroller.offsetY(), 0.0, 0.001);
}

TEST(scrollview_a_scrollTo_command_takes_the_list_off_a_fling) {
  Scroller scroller(14, 4000);
  EXPECT(scroller.manager.fling(scroller.tag, 0, 1200));
  scroller.manager.advanceFling(scroller.tag, kFrame);

  scroller.manager.dispatchCommand(scroller.tag, "scrollTo", folly::dynamic::array(0, 900, false));
  EXPECT_NEAR(scroller.offsetY(), 900.0, 0.001);
  // The fling is gone rather than paused: an app that asked for an offset means
  // that offset, not that offset plus wherever the coast was heading.
  EXPECT(!scroller.manager.advanceFling(scroller.tag, kFrame));
  EXPECT_NEAR(scroller.offsetY(), 900.0, 0.001);
}

TEST(scrollview_decelerationRate_shortens_a_fling) {
  Scroller normal(15, 4000);
  normal.manager.fling(normal.tag, 0, 1200);
  normal.settle();

  Scroller fast(16, 4000, basalt::ScrollMomentum::kFastDeceleration);
  fast.manager.fling(fast.tag, 0, 1200);
  fast.settle();

  // React Native's prop, reaching the model. 'fast' is the smaller number and
  // the shorter fling.
  EXPECT(fast.offsetY() < normal.offsetY());
}

// --- Paging ------------------------------------------------------------------
//
// The arithmetic is tested in test_scroll_snap.cpp; what these assert is the
// wiring -- that the props reach the manager, and that a fling on a paging list
// settles on a boundary instead of coasting.
//
// The container is 300 tall, so a page is 300.

TEST(scrollview_a_fling_on_a_paging_list_settles_on_a_page) {
  Scroller scroller(11, 4000, 0, /*pagingEnabled=*/true);

  // Returns false: a snapping list does not coast, so there is no fling to
  // report having started.
  EXPECT(!scroller.manager.fling(scroller.tag, 0, 1200));
  // The settle is an animation rather than a fling, so it is advanced rather
  // than coasted. Sixty frames is twice the curve's length.
  for (int i = 0; i < 60; i++) {
    scroller.manager.advanceAnimation(scroller.tag, 1.0 / 60.0);
  }
  EXPECT_EQ(scroller.offsetY(), 300.0);
}

TEST(scrollview_a_fling_backwards_settles_on_the_boundary_behind) {
  // Twenty pixels into the second page, flicked back: the answer is that page's
  // start, not the page before it. "The next point in the direction flicked" is
  // symmetric -- forwards from here would be 600 -- and it is what CSS
  // scroll-snap does. Going back two boundaries would mean a small flick could
  // travel further than a large one.
  Scroller scroller(12, 4000, 0, /*pagingEnabled=*/true);
  scroller.manager.dispatchCommand(scroller.tag, "scrollTo", folly::dynamic::array(0, 320, false));
  EXPECT(!scroller.manager.fling(scroller.tag, 0, -1200));
  for (int i = 0; i < 60; i++) {
    scroller.manager.advanceAnimation(scroller.tag, 1.0 / 60.0);
  }
  EXPECT_EQ(scroller.offsetY(), 300.0);
}

TEST(scrollview_a_fling_back_from_a_boundary_reaches_the_previous_page) {
  // Exactly on a boundary there is nothing behind to settle on, so the flick
  // takes the page before it -- which is what stops a list getting stuck.
  Scroller scroller(14, 4000, 0, /*pagingEnabled=*/true);
  scroller.manager.dispatchCommand(scroller.tag, "scrollTo", folly::dynamic::array(0, 300, false));
  EXPECT(!scroller.manager.fling(scroller.tag, 0, -1200));
  for (int i = 0; i < 60; i++) {
    scroller.manager.advanceAnimation(scroller.tag, 1.0 / 60.0);
  }
  EXPECT_EQ(scroller.offsetY(), 0.0);
}

TEST(scrollview_a_fling_on_a_plain_list_still_coasts) {
  // The guard that matters: paging is off for almost every list, and this path
  // runs at the end of every fling.
  Scroller scroller(13, 4000);
  EXPECT(scroller.manager.fling(scroller.tag, 0, 1200));
}

// --- contentOffset -----------------------------------------------------------
//
// An app writes it to open a list part way down, and writes it again to move
// one. The first of those already worked everywhere, through the state --
// `ScrollViewShadowNode::initialStateData` seeds the state from the prop -- and
// a change to the prop afterwards reached no host until 2026-10-10.
//
// The rule is upstream's: applied when the prop *changes*, not whenever it
// differs from where the list is. The difference is the whole entry, and the
// third test is the one that shows it.

TEST(scrollview_an_initial_content_offset_opens_the_list_there) {
  Scroller scroller(20, 4000, 0, false, Point{0, 120});
  EXPECT_NEAR(scroller.offsetY(), 120.0, 0.001);
}

TEST(scrollview_a_changed_content_offset_moves_the_list) {
  Scroller scroller(21, 4000, 0, false, Point{0, 120});
  scroller.manager.update(
      scroller.view, makeScrollView(21, 400, 300, 400, 4000, 0, false, Point{0, 700}));
  EXPECT_NEAR(scroller.offsetY(), 700.0, 0.001);
}

TEST(scrollview_an_unchanged_content_offset_leaves_the_list_where_it_is) {
  // The test that says why this is a "when it changes" rule. React Native
  // re-renders for all sorts of reasons, and every one of them produces an
  // Update mutation carrying the same `contentOffset` the app wrote once. A
  // host that applied it whenever it differed would drag the list back to the
  // top under the person reading it.
  Scroller scroller(22, 4000, 0, false, Point{0, 120});
  scroller.manager.dispatchCommand(scroller.tag, "scrollTo", folly::dynamic::array(0, 900, false));
  EXPECT_NEAR(scroller.offsetY(), 900.0, 0.001);

  scroller.manager.update(
      scroller.view, makeScrollView(22, 400, 300, 400, 4000, 0, false, Point{0, 120}));
  EXPECT_NEAR(scroller.offsetY(), 900.0, 0.001);
}

TEST(scrollview_a_content_offset_takes_the_list_off_a_fling) {
  // The same rule `scrollTo` follows: an app that asked for an offset means
  // that offset, not that offset plus wherever the coast was heading.
  Scroller scroller(23, 4000);
  EXPECT(scroller.manager.fling(scroller.tag, 0, 1200));
  scroller.manager.advanceFling(scroller.tag, kFrame);

  scroller.manager.update(
      scroller.view, makeScrollView(23, 400, 300, 400, 4000, 0, false, Point{0, 500}));
  EXPECT_NEAR(scroller.offsetY(), 500.0, 0.001);
  EXPECT(!scroller.manager.advanceFling(scroller.tag, kFrame));
  EXPECT_NEAR(scroller.offsetY(), 500.0, 0.001);
}

TEST(scrollview_a_content_offset_past_the_content_is_clamped) {
  Scroller scroller(24, 500, 0, false, Point{0, 400});
  // 300 of viewport over 500 of content: 200 is as far as it goes.
  EXPECT_NEAR(scroller.offsetY(), 200.0, 0.001);
}

// --- centerContent -----------------------------------------------------------
//
// Content smaller than the container sits in the middle of it. The arithmetic
// is core/ScrollBounds.h's -- an inset of half the slack at each end, which
// leaves one offset to rest at -- so what these assert is that the prop reaches
// it and that the clamp puts the content there.

TEST(scrollview_center_content_centres_content_that_fits) {
  // 200 of content in a 300 container: 100 of slack, so the content sits 50
  // further down, which is an offset of -50.
  Scroller scroller(30, 200, 0, false, Point{0, 0}, /*centerContent=*/true);
  EXPECT_NEAR(scroller.offsetY(), -50.0, 0.001);
}

TEST(scrollview_content_that_fits_is_not_centred_unless_asked) {
  // The control, and the behaviour of every list that never set the prop.
  Scroller scroller(31, 200);
  EXPECT_NEAR(scroller.offsetY(), 0.0, 0.001);
}

TEST(scrollview_center_content_does_nothing_to_a_list_that_scrolls) {
  // Content longer than the container has no slack to share out, and an inset
  // here would let the list be pulled past its own first row.
  Scroller scroller(32, 4000, 0, false, Point{0, 0}, /*centerContent=*/true);
  EXPECT_NEAR(scroller.offsetY(), 0.0, 0.001);
  scroller.manager.dispatchCommand(scroller.tag, "scrollTo", folly::dynamic::array(0, -50, false));
  EXPECT_NEAR(scroller.offsetY(), 0.0, 0.001);
}

TEST(scrollview_centred_content_stays_centred_when_it_grows) {
  // The inset is recomputed from the content size on every mutation, which is
  // what this asserts: a list that grew past its container stops being centred
  // rather than keeping an inset worked out when it was smaller.
  Scroller scroller(33, 200, 0, false, Point{0, 0}, /*centerContent=*/true);
  EXPECT_NEAR(scroller.offsetY(), -50.0, 0.001);

  scroller.manager.update(
      scroller.view,
      makeScrollView(33, 400, 300, 400, 4000, 0, false, Point{0, 0}, /*centerContent=*/true));
  EXPECT_NEAR(scroller.offsetY(), 0.0, 0.001);
}

// `indicatorStyle`: the prop reaching the thumb's colour. What the colour then
// draws as is tests/test_gtk_paint.cpp's question; this is the wiring.
TEST(scrollview_indicator_style_reaches_the_thumb) {
  Scroller plain(40, 4000);
  gchar *blackDump = rn_view_describe_tree(plain.view);
  const std::string black(blackDump);
  g_free(blackDump);
  // A default thumb prints no colour at all, like every other field that is at
  // its default.
  EXPECT(black.find("scrollbar-v=") != std::string::npos);
  EXPECT(black.find("scrollbar-colour") == std::string::npos);

  Scroller white(41,
                 4000,
                 0,
                 false,
                 Point{0, 0},
                 false,
                 facebook::react::ScrollViewIndicatorStyle::White);
  gchar *whiteDump = rn_view_describe_tree(white.view);
  const std::string reported(whiteDump);
  g_free(whiteDump);
  EXPECT(reported.find("scrollbar-colour=(1,1,1,0.35)") != std::string::npos);
}

// --- maintainVisibleContentPosition ------------------------------------------
//
// The prop a chat list is written with: messages arrive above what the person
// is reading, and the content under their eye must not jump by the height of
// whatever was inserted.
//
// Through the *mounting* manager rather than the scroll manager, which the rest
// of this file uses: the two halves of this run around a transaction, so a test
// that called them itself would not be testing that anything calls them. The
// arithmetic is tested in core/tests/test_scroll_visible_position.cpp.
namespace {

ShadowView makePlainView(Tag tag, float x, float y, float width, float height) {
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

void applyTo(basalt::GtkMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

// A ScrollView with a content view and two hundred-point rows under it, which
// is the smallest tree the prop can be asked about.
void mountList(basalt::GtkMountingManager &manager, const ShadowView &scroller) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(scroller));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, scroller, 0));
  const ShadowView content = makePlainView(11, 0, 0, 400, 1000);
  mutations.push_back(ShadowViewMutation::CreateMutation(content));
  mutations.push_back(ShadowViewMutation::InsertMutation(10, content, 0));
  const ShadowView first = makePlainView(12, 0, 0, 400, 100);
  const ShadowView second = makePlainView(13, 0, 100, 400, 100);
  mutations.push_back(ShadowViewMutation::CreateMutation(first));
  mutations.push_back(ShadowViewMutation::InsertMutation(11, first, 0));
  mutations.push_back(ShadowViewMutation::CreateMutation(second));
  mutations.push_back(ShadowViewMutation::InsertMutation(11, second, 1));
  applyTo(manager, std::move(mutations));
}

// The first row grows by a hundred points, which pushes the second one down by
// the same -- the shape an insertion above the visible content has.
void growTheFirstRow(basalt::GtkMountingManager &manager) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::UpdateMutation(
      makePlainView(12, 0, 0, 400, 100), makePlainView(12, 0, 0, 400, 200), kSurfaceId));
  mutations.push_back(ShadowViewMutation::UpdateMutation(
      makePlainView(13, 0, 100, 400, 100), makePlainView(13, 0, 200, 400, 100), kSurfaceId));
  applyTo(manager, std::move(mutations));
}

double offsetOf(basalt::GtkMountingManager &manager, Tag tag) {
  double x = 0;
  double y = 0;
  rn_view_get_scroll_offset(manager.viewForTag(tag), &x, &y);
  return y;
}

} // namespace

TEST(scrollview_maintain_visible_content_position_holds_the_content_still) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);
  rn_view_set_frame(root, 0, 0, 400, 300);

  facebook::react::ScrollViewMaintainVisibleContentPosition maintain;
  maintain.minIndexForVisible = 0;
  mountList(manager,
            makeScrollView(10,
                           400,
                           300,
                           400,
                           1000,
                           0,
                           false,
                           Point{0, 0},
                           false,
                           facebook::react::ScrollViewIndicatorStyle::Default,
                           maintain));

  // Scrolled to 150: the first row is entirely above the viewport and the
  // second is the first with anything on screen, so the second is the one held
  // still.
  manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 150, false));
  EXPECT_NEAR(offsetOf(manager, 10), 150.0, 0.001);

  growTheFirstRow(manager);

  // The row moved down by a hundred, so the offset did: what was under the eye
  // still is.
  EXPECT_NEAR(offsetOf(manager, 10), 250.0, 0.001);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(scrollview_a_list_that_did_not_ask_lets_the_content_jump) {
  // The control, and the behaviour of every list that never set the prop: the
  // offset stays where it was and the content under it moves.
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);
  rn_view_set_frame(root, 0, 0, 400, 300);

  mountList(manager, makeScrollView(10, 400, 300, 400, 1000));
  manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 150, false));
  growTheFirstRow(manager);

  EXPECT_NEAR(offsetOf(manager, 10), 150.0, 0.001);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(scrollview_min_index_for_visible_skips_a_header) {
  // `minIndexForVisible` of 1 means the first row is not a candidate, so at the
  // top of the list the second one is watched -- and it moves, where the first
  // does not. A list that watched the header would adjust by nothing.
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);
  rn_view_set_frame(root, 0, 0, 400, 300);

  facebook::react::ScrollViewMaintainVisibleContentPosition maintain;
  maintain.minIndexForVisible = 1;
  mountList(manager,
            makeScrollView(10,
                           400,
                           300,
                           400,
                           1000,
                           0,
                           false,
                           Point{0, 0},
                           false,
                           facebook::react::ScrollViewIndicatorStyle::Default,
                           maintain));

  EXPECT_NEAR(offsetOf(manager, 10), 0.0, 0.001);
  growTheFirstRow(manager);
  EXPECT_NEAR(offsetOf(manager, 10), 100.0, 0.001);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(scrollview_a_list_near_the_start_follows_the_new_content) {
  // `autoscrollToTopThreshold`: a list within that many points of the start is
  // taken to the start instead of holding its place, which is what a chat view
  // sitting where the new messages arrive should do. Animated, so the
  // assertion runs the animation out.
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);
  rn_view_set_frame(root, 0, 0, 400, 300);

  // `minIndexForVisible` of 1 so that the row being watched is one that *moves*:
  // at the top of the list the first row is watched and stays put, and nothing
  // -- including this -- happens when the watched child did not move. Which is
  // upstream's rule and is why the threshold alone is not enough to trigger it.
  facebook::react::ScrollViewMaintainVisibleContentPosition maintain;
  maintain.minIndexForVisible = 1;
  maintain.autoscrollToTopThreshold = 50;
  mountList(manager,
            makeScrollView(10,
                           400,
                           300,
                           400,
                           1000,
                           0,
                           false,
                           Point{0, 0},
                           false,
                           facebook::react::ScrollViewIndicatorStyle::Default,
                           maintain));

  manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 20, false));
  growTheFirstRow(manager);

  // The instant half happened: the watched row moved a hundred points down and
  // the offset went with it. The animated half is on its way to the start.
  EXPECT_NEAR(offsetOf(manager, 10), 120.0, 0.001);
  for (int frame = 0; frame < 60; frame++) {
    manager.scrollViews().advanceAnimation(10, 1.0 / 60.0);
  }
  EXPECT_NEAR(offsetOf(manager, 10), 0.0, 0.001);

  manager.destroySurfaceRoot(kSurfaceId);
}
