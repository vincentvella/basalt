// Tests for the fling GTK hands over half-finished.
//
// `<ScrollView>` itself is exercised end to end by `scripts/compare_hosts.sh`
// running `js/scroll.js` against both hosts, which is a better test than
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

#include "GtkScrollView.h"
#include "RnView.h"

#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>

#include <memory>
#include <sstream>

using facebook::react::LayoutMetrics;
using facebook::react::Point;
using facebook::react::Rect;
using facebook::react::ScrollViewProps;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ScrollViewState;
using facebook::react::ShadowNodeFamily;
using facebook::react::ShadowView;
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
                          bool pagingEnabled = false) {
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = width, .height = height}};

  auto props = std::make_shared<ScrollViewProps>();
  props->decelerationRate = decelerationRate;
  props->pagingEnabled = pagingEnabled;

  ScrollViewState data;
  data.contentOffset = Point{0, 0};
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

  Scroller(Tag tag, float contentHeight, float decelerationRate = 0, bool pagingEnabled = false)
      : manager([](Tag) { return facebook::react::EventEmitter::Shared{}; }),
        view(rn_view_new(static_cast<int>(tag))),
        tag(tag) {
    g_object_ref_sink(view);
    rn_view_set_frame(view, 0, 0, 400, 300);
    manager.update(
        view,
        makeScrollView(tag, 400, 300, 400, contentHeight, decelerationRate, pagingEnabled));
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
