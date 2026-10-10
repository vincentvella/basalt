// `<ScrollView>` on AppKit, as far as a test without a window reaches.
//
// Most of what this host's scroll view does needs a display: the wheel arrives
// as an NSEvent, the momentum is the system's, and the indicator is a view that
// has to be drawn. What does not need one is the part React Native drives --
// props arriving as mutations and the offset that results -- and
// `AppKitScrollViewManager` takes an emitter lookup and a view, so it can be
// built on its own exactly as the GTK suite builds its own manager.
//
// An EventEmitter made by hand has no EventDispatcher, so the lookup answering
// nothing is honest: what is observable here is where the content sits.

#include "TestHarness.h"

#include "AppKitMountingManager.h"
#include "AppKitScrollView.h"
#include "ScrollIndicator.h"

#import "RnAppKitView.h"

#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/components/scrollview/ScrollViewState.h>

#include <memory>
#include <optional>
#include <sstream>

using facebook::react::EventEmitter;
using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::ScrollViewProps;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ScrollViewState;
using facebook::react::ShadowNodeFamily;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;
using facebook::react::Tag;

// `Point`, `Rect` and `Size` are not imported by name: Carbon declares all
// three, through Cocoa, and an unqualified one is ambiguous in an Objective-C++
// translation unit. Aliases rather than a `using`, so the geometry below reads
// the way it does in the C++ suites.
using RnPoint = facebook::react::Point;
using RnRect = facebook::react::Rect;
using RnSize = facebook::react::Size;

namespace {

constexpr SurfaceId kSurfaceId = 1;

ShadowView makeScrollView(Tag tag,
                          float contentHeight,
                          RnPoint contentOffset = RnPoint{0, 0},
                          bool centerContent = false,
                          facebook::react::ScrollViewIndicatorStyle indicatorStyle =
                              facebook::react::ScrollViewIndicatorStyle::Default,
                          std::optional<facebook::react::ScrollViewMaintainVisibleContentPosition>
                              maintainVisible = std::nullopt) {
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 400, .height = 300}};

  auto props = std::make_shared<ScrollViewProps>();
  props->contentOffset = contentOffset;
  props->centerContent = centerContent;
  props->indicatorStyle = indicatorStyle;
  props->maintainVisibleContentPosition = maintainVisible;

  ScrollViewState data;
  // The prop and the state carry the same value, which is what React Native
  // produces: `ScrollViewShadowNode::initialStateData` seeds the state from the
  // prop. A test that set only one of them would be testing a shape that never
  // arrives.
  data.contentOffset = contentOffset;
  data.contentBoundingRect = RnRect{.origin = {.x = 0, .y = 0},
                                  .size = RnSize{.width = 400, .height = contentHeight}};

  ShadowView view;
  view.componentName = "ScrollView";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  // An empty family: nothing can be committed back through it, so the state
  // write-back is not observable from here.
  view.state = std::make_shared<const ScrollViewShadowNode::ConcreteState>(
      std::make_shared<const ScrollViewState>(data), ShadowNodeFamily::Weak{});
  return view;
}

// A manager with one scroll view registered against a real RnAppKitView.
struct Scroller {
  basalt::AppKitScrollViewManager manager;
  RnAppKitView *view;
  Tag tag;

  Scroller(Tag tag,
           float contentHeight,
           RnPoint contentOffset = RnPoint{0, 0},
           bool centerContent = false,
           facebook::react::ScrollViewIndicatorStyle indicatorStyle =
               facebook::react::ScrollViewIndicatorStyle::Default)
      : manager([](Tag) { return EventEmitter::Shared{}; }),
        view([RnAppKitView viewWithTag:static_cast<int>(tag)]),
        tag(tag) {
    [view setRnFrameX:0 y:0 width:400 height:300];
    manager.update(
        view,
        makeScrollView(tag, contentHeight, contentOffset, centerContent, indicatorStyle));
  }

  ~Scroller() { manager.remove(tag); }

  Scroller(const Scroller &) = delete;
  Scroller &operator=(const Scroller &) = delete;

  double offsetY() const { return view.rnScrollOffset.y; }
};

} // namespace

// --- contentOffset ----------------------------------------------------------
//
// The first value an app writes reaches here through the *state*, which
// ScrollViewShadowNode seeds from the prop, and every host already adopted it.
// A later change arrives as an ordinary prop update and reached no host until
// 2026-10-10.

TEST(appkit_scrollview_an_initial_content_offset_opens_the_list_there) {
  @autoreleasepool {
    Scroller scroller(10, 4000, RnPoint{0, 120});
    EXPECT_NEAR(scroller.offsetY(), 120.0, 0.001);
  }
}

TEST(appkit_scrollview_a_changed_content_offset_moves_the_list) {
  @autoreleasepool {
    Scroller scroller(11, 4000, RnPoint{0, 120});
    scroller.manager.update(scroller.view, makeScrollView(11, 4000, RnPoint{0, 700}));
    EXPECT_NEAR(scroller.offsetY(), 700.0, 0.001);
  }
}

TEST(appkit_scrollview_an_unchanged_content_offset_leaves_the_list_where_it_is) {
  @autoreleasepool {
    // The reason this is a "when it changes" rule rather than a comparison
    // against the list: React Native re-renders for all sorts of reasons, and
    // every one of them carries the same `contentOffset` the app wrote once.
    Scroller scroller(12, 4000, RnPoint{0, 120});
    scroller.manager.dispatchCommand(
        scroller.tag, "scrollTo", folly::dynamic::array(0, 900, false));
    EXPECT_NEAR(scroller.offsetY(), 900.0, 0.001);

    scroller.manager.update(scroller.view, makeScrollView(12, 4000, RnPoint{0, 120}));
    EXPECT_NEAR(scroller.offsetY(), 900.0, 0.001);
  }
}

TEST(appkit_scrollview_a_content_offset_past_the_content_is_clamped) {
  @autoreleasepool {
    // 300 of viewport over 500 of content: 200 is as far as it goes.
    Scroller scroller(13, 500, RnPoint{0, 400});
    EXPECT_NEAR(scroller.offsetY(), 200.0, 0.001);
  }
}

// --- centerContent ----------------------------------------------------------
//
// The arithmetic is core/ScrollBounds.h's: an inset of half the slack at each
// end, which leaves one offset to rest at. What this asserts is that the prop
// reaches it on this host and that the clamp puts the content there.

TEST(appkit_scrollview_center_content_centres_content_that_fits) {
  @autoreleasepool {
    // 200 of content in a 300 container: 100 of slack, so the content sits 50
    // further down, which is an offset of -50.
    Scroller scroller(20, 200, RnPoint{0, 0}, /*centerContent=*/true);
    EXPECT_NEAR(scroller.offsetY(), -50.0, 0.001);
  }
}

TEST(appkit_scrollview_content_that_fits_is_not_centred_unless_asked) {
  @autoreleasepool {
    Scroller scroller(21, 200);
    EXPECT_NEAR(scroller.offsetY(), 0.0, 0.001);
  }
}

TEST(appkit_scrollview_center_content_does_nothing_to_a_list_that_scrolls) {
  @autoreleasepool {
    Scroller scroller(22, 4000, RnPoint{0, 0}, /*centerContent=*/true);
    EXPECT_NEAR(scroller.offsetY(), 0.0, 0.001);
  }
}

// --- indicatorStyle ---------------------------------------------------------
//
// The prop reaching the thumb's colour. What that colour draws as is
// tests/test_appkit_scrollbar.mm's question, where the overlay is rasterised;
// this is the wiring, and the colour is readable straight off the view.

TEST(appkit_scrollview_indicator_style_reaches_the_thumb) {
  @autoreleasepool {
    Scroller white(30,
                   4000,
                   RnPoint{0, 0},
                   false,
                   facebook::react::ScrollViewIndicatorStyle::White);
    NSColor *const colour =
        [white.view.rnScrollIndicatorColour colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    EXPECT(colour != nil);
    if (colour == nil) {
      return;
    }
    EXPECT_NEAR(colour.redComponent, 1.0, 0.001);
    EXPECT_NEAR(colour.greenComponent, 1.0, 0.001);
    EXPECT_NEAR(colour.blueComponent, 1.0, 0.001);
    EXPECT_NEAR(colour.alphaComponent, basalt::kScrollIndicatorAlpha, 0.001);
  }
}

TEST(appkit_scrollview_a_default_indicator_is_black) {
  @autoreleasepool {
    // Black rather than nothing: the manager answers the prop on every update,
    // so a list that never set it still has a colour -- core's.
    Scroller plain(31, 4000);
    NSColor *const colour =
        [plain.view.rnScrollIndicatorColour colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    EXPECT(colour != nil);
    if (colour == nil) {
      return;
    }
    EXPECT_NEAR(colour.redComponent, 0.0, 0.001);
    EXPECT_NEAR(colour.alphaComponent, basalt::kScrollIndicatorAlpha, 0.001);
  }
}

// --- maintainVisibleContentPosition ------------------------------------------
//
// Through the *mounting* manager rather than the scroll manager the rest of
// this file uses: the two halves of this prop run around a transaction, so a
// test that called them itself would not be testing that anything calls them.
// The arithmetic is core/tests/test_scroll_visible_position.cpp's.
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

void applyTo(basalt::AppKitMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

// A ScrollView with a content view and two hundred-point rows under it, which
// is the smallest tree the prop can be asked about.
void mountList(basalt::AppKitMountingManager &manager, const ShadowView &scroller) {
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
void growTheFirstRow(basalt::AppKitMountingManager &manager) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::UpdateMutation(
      makePlainView(12, 0, 0, 400, 100), makePlainView(12, 0, 0, 400, 200), kSurfaceId));
  mutations.push_back(ShadowViewMutation::UpdateMutation(
      makePlainView(13, 0, 100, 400, 100), makePlainView(13, 0, 200, 400, 100), kSurfaceId));
  applyTo(manager, std::move(mutations));
}

double offsetOf(basalt::AppKitMountingManager &manager, Tag tag) {
  return manager.viewForTag(tag).rnScrollOffset.y;
}

facebook::react::ScrollViewMaintainVisibleContentPosition maintainFrom(int minIndex) {
  facebook::react::ScrollViewMaintainVisibleContentPosition maintain;
  maintain.minIndexForVisible = minIndex;
  return maintain;
}

ShadowView listScroller(
    std::optional<facebook::react::ScrollViewMaintainVisibleContentPosition> maintain) {
  return makeScrollView(10,
                        1000,
                        RnPoint{0, 0},
                        false,
                        facebook::react::ScrollViewIndicatorStyle::Default,
                        maintain);
}

} // namespace

TEST(appkit_scrollview_maintain_visible_content_position_holds_the_content_still) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    mountList(manager, listScroller(maintainFrom(0)));

    // Scrolled to 150: the first row is entirely above the viewport and the
    // second is the first with anything on screen, so the second is held still.
    manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 150, false));
    EXPECT_NEAR(offsetOf(manager, 10), 150.0, 0.001);

    growTheFirstRow(manager);
    EXPECT_NEAR(offsetOf(manager, 10), 250.0, 0.001);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(appkit_scrollview_a_list_that_did_not_ask_lets_the_content_jump) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    mountList(manager, listScroller(std::nullopt));
    manager.applyCommand(10, "scrollTo", folly::dynamic::array(0, 150, false));
    growTheFirstRow(manager);

    EXPECT_NEAR(offsetOf(manager, 10), 150.0, 0.001);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(appkit_scrollview_min_index_for_visible_skips_a_header) {
  @autoreleasepool {
    // At the top of the list the first row is the visible one, and an index of
    // 1 takes it out of the running -- so the second row is watched, and that
    // one moves.
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    mountList(manager, listScroller(maintainFrom(1)));
    EXPECT_NEAR(offsetOf(manager, 10), 0.0, 0.001);
    growTheFirstRow(manager);
    EXPECT_NEAR(offsetOf(manager, 10), 100.0, 0.001);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}
