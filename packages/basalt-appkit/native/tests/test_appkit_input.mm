// Tests for hit testing and the touch state machine.
//
// Hit testing is the part of input most likely to be quietly wrong, and it is a
// pure function of the view tree, so it can be tested without a mouse, a window
// server, or permission to synthesise an event. That last one matters:
// synthesising a real click on macOS means CGEvent, which needs accessibility
// permission an automated run does not have, so everything below AppKit's event
// delivery is tested here instead.
//
// Unlike the GTK equivalent, none of this needs a window. `gtk_widget_pick`
// skips unmapped widgets, so those tests have to show a window and pump the
// frame clock; `RnAppKitHitTest` walks frames and is answerable straight away.

#include "TestHarness.h"

#include "PointerButtons.h"

#import "AppKitFocus.h"
#import "AppKitTouchDispatcher.h"
#import "RnAppKitView.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>

#include <cstdint>
#include <sstream>

using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

RnAppKitView *box(NSInteger tag, CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
  RnAppKitView *view = [RnAppKitView viewWithTag:tag];
  [view setRnFrameX:x y:y width:w height:h];
  return view;
}

NSInteger hit(RnAppKitView *root, CGFloat x, CGFloat y) {
  RnAppKitView *view = RnAppKitHitTest(root, x, y);
  return view == nil ? 0 : view.rnTag;
}

ShadowView makeView(Tag tag, float x, float y, float width, float height) {
  auto props = std::make_shared<ViewProps>();
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = x, .y = y}, .size = {.width = width, .height = height}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

void apply(basalt::AppKitMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

} // namespace

TEST(hit_test_finds_the_view_at_a_point) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *child = box(10, 20, 30, 50, 40);
    [root insertRnChild:child atIndex:0];

    EXPECT_EQ((long)hit(root, 25, 35), 10L);
    // Inside the root, outside the child.
    EXPECT_EQ((long)hit(root, 5, 5), 1L);
    // Outside everything.
    EXPECT_EQ((long)hit(root, 500, 500), 0L);
  }
}

// The point is top-left relative, like every other coordinate here. An
// unflipped hit test would find the child 30 from the *bottom* instead, which
// is the same bug `isFlipped` exists to prevent and would be invisible in a
// vertically symmetric layout.
TEST(hit_test_uses_top_left_coordinates) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *top = box(10, 0, 0, 200, 20);
    [root insertRnChild:top atIndex:0];

    EXPECT_EQ((long)hit(root, 100, 10), 10L);
    EXPECT_EQ((long)hit(root, 100, 190), 1L);
  }
}

// A transformed view is hit where it is *drawn*, not where its frame is.
//
// The two are the same for every untransformed view, which is why subtracting
// the frame origin passed every test here for as long as it did. A translation
// is the cheapest case that tells them apart: the frame does not move, the
// drawing does, and a press has to follow the drawing.
TEST(hit_test_follows_a_translated_view) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *child = box(10, 0, 0, 40, 40);
    // matrix3d, CSS order: identity with a 100pt shift right and 50 down.
    const float moved[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 100, 50, 0, 1};
    [child setRnTransform:moved];
    [root insertRnChild:child atIndex:0];

    // Where it is drawn.
    EXPECT_EQ((long)hit(root, 120, 70), 10L);
    // Where its frame is, and where it is no longer drawn.
    EXPECT_EQ((long)hit(root, 20, 20), 1L);
  }
}

// A rotation, which is the case that cannot be got right by adjusting an
// offset: the corners of the drawn box are outside its frame and its centre is
// in the same place.
TEST(hit_test_follows_a_rotated_view) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *child = box(10, 50, 50, 100, 20);
    // 90 degrees. A box 100 wide and 20 tall becomes 20 wide and 100 tall,
    // about its own centre at (100, 60).
    const float quarterTurn[16] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    [child setRnTransform:quarterTurn];
    [root insertRnChild:child atIndex:0];

    // The centre does not move, so it is still the child.
    EXPECT_EQ((long)hit(root, 100, 60), 10L);
    // Above and below the centre: inside the turned box, outside the frame.
    EXPECT_EQ((long)hit(root, 100, 20), 10L);
    EXPECT_EQ((long)hit(root, 100, 100), 10L);
    // Left and right of the centre: inside the frame, outside the turned box.
    EXPECT_EQ((long)hit(root, 60, 60), 1L);
    EXPECT_EQ((long)hit(root, 140, 60), 1L);
  }
}

// `scale: 0` is legal, draws nothing, and has no inverse. Skipped rather than
// inverted, and the press carries on to what is behind it.
TEST(hit_test_skips_a_view_scaled_to_nothing) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *child = box(10, 0, 0, 100, 100);
    const float flattened[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    [child setRnTransform:flattened];
    [root insertRnChild:child atIndex:0];

    EXPECT_EQ((long)hit(root, 50, 50), 1L);
  }
}

// --------------------------------------------------------------------------
// offsetPoint
// --------------------------------------------------------------------------
//
// `Touch::offsetPoint` is where inside the target a press landed, and what
// `locationX`/`locationY` are built from. It carried the *page* point until
// now, which is right only for a view at the surface's origin.
//
// No unit test can read a touch event here -- see docs/backlog/testing.md --
// so what is tested is the walk the dispatcher uses, which is the whole of the
// arithmetic.

TEST(page_to_local_subtracts_the_ancestors) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 400);
    RnAppKitView *middle = box(10, 50, 60, 300, 300);
    RnAppKitView *leaf = box(20, 20, 30, 100, 100);
    [root insertRnChild:middle atIndex:0];
    [middle insertRnChild:leaf atIndex:0];

    NSPoint local = NSZeroPoint;
    EXPECT([leaf rnPageToLocal:NSMakePoint(100, 120) fromRoot:root into:&local]);
    // 100 - 50 - 20, and 120 - 60 - 30.
    EXPECT_NEAR(local.x, 30.0, 0.001);
    EXPECT_NEAR(local.y, 30.0, 0.001);
  }
}

// The root's own point is the page point, which is the case that made the old
// behaviour look right for as long as it did.
TEST(page_to_local_at_the_root_is_the_page_point) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 400);
    NSPoint local = NSZeroPoint;
    EXPECT([root rnPageToLocal:NSMakePoint(100, 120) fromRoot:root into:&local]);
    EXPECT_NEAR(local.x, 100.0, 0.001);
    EXPECT_NEAR(local.y, 120.0, 0.001);
  }
}

TEST(page_to_local_follows_a_transformed_ancestor) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 400);
    RnAppKitView *moved = box(10, 0, 0, 100, 100);
    // Shifted 100 right and 50 down by a transform rather than by its frame.
    const float translate[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 100, 50, 0, 1};
    [moved setRnTransform:translate];
    [root insertRnChild:moved atIndex:0];

    NSPoint local = NSZeroPoint;
    EXPECT([moved rnPageToLocal:NSMakePoint(110, 60) fromRoot:root into:&local]);
    // The press is 10 into the view as drawn, not 110.
    EXPECT_NEAR(local.x, 10.0, 0.001);
    EXPECT_NEAR(local.y, 10.0, 0.001);
  }
}

// A scrolled ancestor moves its children, and the offset has to come back out
// -- the same offset the hit test adds on the way down.
TEST(page_to_local_follows_a_scrolled_ancestor) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 400);
    RnAppKitView *scroller = box(10, 0, 0, 200, 200);
    RnAppKitView *row = box(20, 0, 300, 200, 50);
    [root insertRnChild:scroller atIndex:0];
    [scroller insertRnChild:row atIndex:0];
    [scroller setRnScrollOffsetX:0 y:280];

    NSPoint local = NSZeroPoint;
    // The row starts at 300 in content space and the view is scrolled 280, so
    // it is drawn 20 down -- and a press there is at the row's own top.
    EXPECT([row rnPageToLocal:NSMakePoint(10, 20) fromRoot:root into:&local]);
    EXPECT_NEAR(local.x, 10.0, 0.001);
    EXPECT_NEAR(local.y, 0.0, 0.001);
  }
}

TEST(page_to_local_refuses_a_chain_with_no_inverse) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 400);
    RnAppKitView *flat = box(10, 0, 0, 100, 100);
    const float flattened[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    [flat setRnTransform:flattened];
    [root insertRnChild:flat atIndex:0];

    NSPoint local = NSMakePoint(-1, -1);
    EXPECT(![flat rnPageToLocal:NSMakePoint(10, 10) fromRoot:root into:&local]);
    // Untouched, so the caller's fallback is what is used.
    EXPECT_NEAR(local.x, -1.0, 0.001);
  }
}

// `backfaceVisibility: 'hidden'` takes the view out of hit testing as well as
// out of the picture -- the back of a card is not clickable. The hit test
// already skips hidden views, so what this checks is that turning away is what
// hides it.
TEST(hit_test_skips_a_view_turned_away) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *card = box(10, 0, 0, 100, 100);
    [card setRnHidesBackFace:YES];
    [root insertRnChild:card atIndex:0];

    // Facing the viewer: it is hit.
    EXPECT_EQ((long)hit(root, 50, 50), 10L);

    // Half a turn about Y, which mirrors it.
    const float flipped[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
    [card setRnTransform:flipped];
    EXPECT_EQ((long)hit(root, 50, 50), 1L);

    // And back again.
    [card setRnTransform:nullptr];
    EXPECT_EQ((long)hit(root, 50, 50), 10L);
  }
}

// Without the prop a turned view is still drawn and still clickable, which is
// what `backfaceVisibility: 'visible'` means and is the default.
TEST(hit_test_keeps_a_turned_view_that_did_not_ask_to_hide) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *card = box(10, 0, 0, 100, 100);
    [root insertRnChild:card atIndex:0];

    const float flipped[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
    [card setRnTransform:flipped];
    EXPECT_EQ((long)hit(root, 50, 50), 10L);
  }
}

// The two reasons a view can be hidden are independent, and Fabric applies
// them through different calls: props first, then layout metrics. So a card
// turned away from the viewer had `setRnHidden:NO` written over the top of it
// on the very same mount, and came back -- which is how this looked in the
// demo long after the unit tests above were passing.
TEST(display_none_does_not_reveal_a_back_face) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *card = box(10, 0, 0, 100, 100);
    [card setRnHidesBackFace:YES];
    [root insertRnChild:card atIndex:0];

    const float flipped[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
    [card setRnTransform:flipped];
    EXPECT_EQ((long)hit(root, 50, 50), 1L);

    // What applyLayoutMetrics says about every view that is not display: none.
    [card setRnHidden:NO];
    EXPECT_EQ((long)hit(root, 50, 50), 1L);
  }
}

// And the other way: a back face that is facing the viewer must not reveal a
// view the app itself hid.
TEST(a_facing_back_face_does_not_reveal_display_none) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *card = box(10, 0, 0, 100, 100);
    [card setRnHidden:YES];
    [card setRnHidesBackFace:YES];
    [root insertRnChild:card atIndex:0];

    EXPECT_EQ((long)hit(root, 50, 50), 1L);

    const float flipped[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
    [card setRnTransform:flipped];
    [card setRnTransform:nullptr];
    EXPECT_EQ((long)hit(root, 50, 50), 1L);
  }
}

// Turning the prop off shows a view it had hidden. The early return this used
// to have left the view hidden for good.
TEST(clearing_the_back_face_prop_shows_the_view_again) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *card = box(10, 0, 0, 100, 100);
    [card setRnHidesBackFace:YES];
    [root insertRnChild:card atIndex:0];

    const float flipped[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
    [card setRnTransform:flipped];
    EXPECT_EQ((long)hit(root, 50, 50), 1L);

    [card setRnHidesBackFace:NO];
    EXPECT_EQ((long)hit(root, 50, 50), 10L);
  }
}

TEST(hit_test_finds_the_deepest_view) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *outer = box(10, 10, 10, 100, 100);
    RnAppKitView *inner = box(11, 10, 10, 20, 20);
    [root insertRnChild:outer atIndex:0];
    [outer insertRnChild:inner atIndex:0];

    // Coordinates are parent-relative on the way down: (25,25) in the root is
    // (15,15) in outer, which is (5,5) in inner.
    EXPECT_EQ((long)hit(root, 25, 25), 11L);
    EXPECT_EQ((long)hit(root, 50, 50), 10L);
  }
}

// Later siblings draw on top, so they are hit first. An implementation that
// walked the subviews array forwards would return whichever overlapping view
// happened to be added first.
TEST(hit_test_prefers_the_topmost_sibling) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *under = box(10, 0, 0, 100, 100);
    RnAppKitView *over = box(11, 0, 0, 100, 100);
    [root insertRnChild:under atIndex:0];
    [root insertRnChild:over atIndex:1];

    EXPECT_EQ((long)hit(root, 50, 50), 11L);
  }
}

// The same question test_win32_hittest.cpp asks, in the same order and with the
// same tags: a press has to follow the paint order rather than the child list,
// or a view drawn behind another still swallows its clicks.
TEST(hit_test_follows_z_index) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 400);
    RnAppKitView *under = box(2, 0, 0, 200, 200);
    RnAppKitView *over = box(3, 0, 0, 200, 200);
    [root insertRnChild:under atIndex:0];
    [root insertRnChild:over atIndex:1];

    [over setRnZIndex:-1];
    EXPECT_EQ((long)hit(root, 100, 100), 2L);
  }
}

TEST(z_index_reorders_painting_and_not_the_child_list) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *first = box(30, 0, 0, 10, 10);
    RnAppKitView *second = box(31, 0, 0, 10, 10);
    [root insertRnChild:first atIndex:0];
    [root insertRnChild:second atIndex:1];

    [first setRnZIndex:10];
    [second setRnZIndex:1];

    // Fabric indexes into the child list on every Insert and Remove, so that
    // list must not be resorted -- only what paints on top of what.
    EXPECT_EQ((long)((RnAppKitView *)root.subviews.firstObject).rnTag, 30L);
    EXPECT_EQ((long)((RnAppKitView *)root.subviews.lastObject).rnTag, 31L);

    NSArray<RnAppKitView *> *painted = [root rnChildrenInPaintOrder];
    EXPECT_EQ((long)painted.firstObject.rnTag, 31L);
    EXPECT_EQ((long)painted.lastObject.rnTag, 30L);
  }
}

// Equal values keep document order, which is what CSS and React Native both
// promise and what a non-stable sort would quietly break.
TEST(z_index_ties_keep_document_order) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    for (NSInteger tag = 40; tag < 44; tag++) {
      RnAppKitView *child = box(tag, 0, 0, 10, 10);
      [child setRnZIndex:(tag == 43 ? 5 : 2)];
      [root insertRnChild:child atIndex:tag - 40];
    }
    NSArray<RnAppKitView *> *painted = [root rnChildrenInPaintOrder];
    EXPECT_EQ((long)painted[0].rnTag, 40L);
    EXPECT_EQ((long)painted[1].rnTag, 41L);
    EXPECT_EQ((long)painted[2].rnTag, 42L);
    EXPECT_EQ((long)painted[3].rnTag, 43L);
  }
}

// display: 'none' takes a view out of layout and painting, and it has to come
// out of hit testing too -- otherwise an invisible view swallows presses meant
// for whatever is behind it.
TEST(hit_test_skips_hidden_views) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 200, 200);
    RnAppKitView *child = box(10, 0, 0, 100, 100);
    child.hidden = YES;
    [root insertRnChild:child atIndex:0];

    EXPECT_EQ((long)hit(root, 50, 50), 1L);
  }
}

// Hit testing has to follow the scroll, and it does so without knowing what a
// ScrollView is: the point is translated into bounds space, and a scrolled view
// has a non-zero bounds origin. Getting this wrong means a list that scrolls
// correctly and whose rows respond to presses meant for the row above.
TEST(hit_test_follows_the_scroll_offset) {
  @autoreleasepool {
    RnAppKitView *scroller = box(1, 0, 0, 200, 100);
    RnAppKitView *content = box(2, 0, 0, 200, 600);
    RnAppKitView *lower = box(10, 0, 200, 200, 50);
    [scroller insertRnChild:content atIndex:0];
    [content insertRnChild:lower atIndex:0];

    // Unscrolled, the row at y=200 is below the 100-tall viewport.
    EXPECT_EQ((long)hit(scroller, 100, 50), 2L);

    [scroller setRnScrollOffsetX:0 y:200];

    // Scrolled to it, the same screen point is now that row.
    EXPECT_EQ((long)hit(scroller, 100, 10), 10L);
    // And a point past its bottom is the content again.
    EXPECT_EQ((long)hit(scroller, 100, 80), 2L);
  }
}

TEST(hit_test_survives_a_nil_root) {
  @autoreleasepool {
    EXPECT(RnAppKitHitTest(nil, 0, 0) == nil);
  }
}

// The dispatcher against a real mounted tree: a tap has to find the tag the
// mounting manager registered, not just some view.
TEST(a_synthesised_tap_finds_the_mounted_view) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(10, 40, 50, 120, 60)));
    mutations.push_back(
        ShadowViewMutation::InsertMutation(kSurfaceId, makeView(10, 40, 50, 120, 60), 0));
    apply(manager, std::move(mutations));

    EXPECT_EQ((long)basalt::hitTestTag(root, 50, 60), 10L);
    EXPECT_EQ((long)basalt::hitTestTag(root, 5, 5), (long)kSurfaceId);

    // No emitter is attached to these hand-built shadow views, so the tap has
    // nowhere to deliver. It must not crash: that is exactly the shape of a
    // press arriving between a Remove and its Delete.
    basalt::AppKitTouchDispatcher dispatcher(&manager, root);
    dispatcher.synthesiseTap(50, 60);
    dispatcher.synthesiseTap(5000, 5000);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A move with no button down is hover, and the touch model has no place for it.
// Reporting it would look to the responder system like a finger dragging across
// the screen at all times, which cancels every press before it can fire.
TEST(a_move_before_a_press_is_ignored) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    basalt::AppKitTouchDispatcher dispatcher(&manager, root);
    // Nothing to assert beyond "does not crash and does not start a gesture";
    // the state it must not enter is private, so this pins the behaviour that
    // an end with no start is also a no-op.
    dispatcher.dispatchTouchMove(10, 10);
    dispatcher.dispatchTouchEnd(10, 10, basalt::PointerButton::Primary);
    dispatcher.dispatchTouchCancel();

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// The root forwards mouse events to whatever handler it was given, and a view
// with no handler of its own walks up to find one. Without that, a press on a
// child would reach nothing.
TEST(a_child_forwards_the_mouse_to_the_root_handler) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];
    RnAppKitView *child = box(10, 0, 0, 100, 100);
    [root insertRnChild:child atIndex:0];

    EXPECT(root.rnInputHandler == nil);
    // Its own pool: a weak reference is zeroed in dealloc, and reading one
    // retains and autoreleases the result, so the assertion after the scope
    // only means anything once the pool that saw those has drained.
    @autoreleasepool {
      basalt::AppKitTouchDispatcher dispatcher(&manager, root);
      EXPECT(root.rnInputHandler != nil);
      // The child has none of its own and is expected to find the root's.
      EXPECT(child.rnInputHandler == nil);
    }
    // The dispatcher is gone; the root's reference is weak, so it went with it
    // rather than leaving a dangling handler behind.
    EXPECT(root.rnInputHandler == nil);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// Hover is fed from the same hit test as touches, and differs in one place
// that matters: it only runs for views that asked for it. The mask comes off
// the props ReactCommon parsed, on every Create and Update, which is what makes
// a cursor crossing an app that uses no hover cost nothing.
TEST(a_hover_listener_is_remembered_from_the_props) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowView listening = makeView(10, 40, 50, 120, 60);
    auto props = std::make_shared<ViewProps>();
    props->events[facebook::react::ViewEvents::Offset::PointerEnter] = true;
    listening.props = props;

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(listening));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, listening, 0));
    apply(manager, std::move(mutations));

    EXPECT_EQ(manager.hoverListenersForTag(10), (std::uint16_t)basalt::HoverListenerEnter);
    // A view that never asked is not stored at all.
    EXPECT_EQ(manager.hoverListenersForTag(kSurfaceId), (std::uint16_t)basalt::HoverListenerNone);

    // An update can take the listener away again, and leaving the old mask
    // behind would mean dispatching to a view that stopped listening.
    ShadowViewMutationList updates;
    updates.push_back(ShadowViewMutation::UpdateMutation(
        listening, makeView(10, 40, 50, 120, 60), kSurfaceId));
    apply(manager, std::move(updates));
    EXPECT_EQ(manager.hoverListenersForTag(10), (std::uint16_t)basalt::HoverListenerNone);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// The same shape as the synthesised tap: no emitter is attached to these
// hand-built shadow views, so every hover has nowhere to deliver and must not
// crash -- which is also what a cursor moving during a surface teardown looks
// like from here.
TEST(a_synthesised_hover_walks_the_tree_without_an_emitter) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(10, 40, 50, 120, 60)));
    mutations.push_back(
        ShadowViewMutation::InsertMutation(kSurfaceId, makeView(10, 40, 50, 120, 60), 0));
    apply(manager, std::move(mutations));

    basalt::AppKitTouchDispatcher dispatcher(&manager, root);
    dispatcher.synthesiseHover(50, 60);
    dispatcher.synthesiseHover(5, 5);
    // Negative means the cursor left the surface.
    dispatcher.synthesiseHover(-1, -1);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// `pointerEvents`, which decides what a press can land on rather than what is
// drawn. Four values, and the two in the middle are the ones worth testing:
// `none` and `auto` are obvious, `box-none` and `box-only` are the pair
// everyone gets the wrong way round.
TEST(pointer_events_none_passes_a_press_through_to_what_is_behind) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 300);
    RnAppKitView *behind = box(10, 0, 0, 200, 200);
    RnAppKitView *over = box(11, 0, 0, 200, 200);
    [root insertRnChild:behind atIndex:0];
    [root insertRnChild:over atIndex:1];

    // Painted last, so it is on top and takes the press.
    EXPECT_EQ(hit(root, 50, 50), 11L);

    over.rnPointerEvents = RnAppKitPointerEventsNone;
    EXPECT_EQ(hit(root, 50, 50), 10L);
  }
}

TEST(pointer_events_none_takes_the_children_with_it) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 300);
    RnAppKitView *over = box(11, 0, 0, 200, 200);
    RnAppKitView *inside = box(12, 0, 0, 100, 100);
    [over insertRnChild:inside atIndex:0];
    [root insertRnChild:over atIndex:0];

    EXPECT_EQ(hit(root, 50, 50), 12L);
    // The whole subtree leaves hit testing, not just the view the prop is on.
    over.rnPointerEvents = RnAppKitPointerEventsNone;
    EXPECT_EQ(hit(root, 50, 50), 1L);
  }
}

TEST(pointer_events_box_none_is_transparent_and_its_children_are_not) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 300);
    RnAppKitView *behind = box(10, 0, 0, 300, 300);
    RnAppKitView *overlay = box(11, 0, 0, 300, 300);
    RnAppKitView *button = box(12, 0, 0, 100, 100);
    [overlay insertRnChild:button atIndex:0];
    [root insertRnChild:behind atIndex:0];
    [root insertRnChild:overlay atIndex:1];

    overlay.rnPointerEvents = RnAppKitPointerEventsBoxNone;
    // Over the button: the overlay's child is still a target.
    EXPECT_EQ(hit(root, 50, 50), 12L);
    // Over the overlay and nothing inside it: the press belongs to the view
    // *behind*, not to the overlay's parent. This is the whole reason the mode
    // exists -- an absolutely-positioned layer that does not block what it
    // covers -- and returning the parent here would look right until something
    // was underneath.
    EXPECT_EQ(hit(root, 200, 200), 10L);
  }
}

TEST(pointer_events_box_only_swallows_presses_meant_for_its_children) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 400, 300);
    RnAppKitView *panel = box(11, 0, 0, 300, 300);
    RnAppKitView *button = box(12, 0, 0, 100, 100);
    [panel insertRnChild:button atIndex:0];
    [root insertRnChild:panel atIndex:0];

    EXPECT_EQ(hit(root, 50, 50), 12L);
    panel.rnPointerEvents = RnAppKitPointerEventsBoxOnly;
    // The press lands on the panel even though it is over the button, which is
    // what makes a disabled panel disable everything in it.
    EXPECT_EQ(hit(root, 50, 50), 11L);
    EXPECT_EQ(hit(root, 200, 200), 11L);
  }
}

TEST(pointer_events_reaches_the_view_from_the_props_and_the_tree_dump) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];

    ShadowView blocked = makeView(10, 0, 0, 100, 100);
    auto props = std::make_shared<ViewProps>();
    props->pointerEvents = facebook::react::PointerEventsMode::BoxNone;
    blocked.props = props;

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(blocked));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, blocked, 0));
    apply(manager, std::move(mutations));

    EXPECT_EQ((long)manager.viewForTag(10).rnPointerEvents,
              (long)RnAppKitPointerEventsBoxNone);
    // In the dump, because it is invisible otherwise: a view with this prop is
    // drawn exactly like one without, so the cross-host diff can only see the
    // prop arrived if each host prints it.
    NSString *tree = [root describeTree];
    EXPECT([tree containsString:@"pe=box-none"]);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// Keyboard focus: what Tab stops on, in what order, and what Enter does.
//
// A window, because focus is the one part of input that needs one: AppKit's
// first responder belongs to a window, and `makeFirstResponder:` has nowhere to
// put it without one. The window is never ordered in front -- these run in an
// automated session that has no front.
namespace {

ShadowView makeAccessibleView(Tag tag, float y, bool accessible) {
  auto props = std::make_shared<facebook::react::ViewProps>();
  props->accessible = accessible;

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = y}, .size = {.width = 200, .height = 40}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

void mountAll(basalt::AppKitMountingManager &manager, std::initializer_list<ShadowView> views) {
  ShadowViewMutationList mutations;
  int index = 0;
  for (const ShadowView &view : views) {
    mutations.push_back(ShadowViewMutation::CreateMutation(view));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, view, index++));
  }
  apply(manager, std::move(mutations));
}

} // namespace

TEST(focus_accessible_views_are_the_ones_tab_stops_on) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:400];
    // React Native's `focusable` prop never reaches this platform, so
    // `accessible` is the signal -- and it is what <Pressable> sets on
    // everything it renders.
    mountAll(manager,
             {makeAccessibleView(10, 0, true),
              makeAccessibleView(11, 50, false),
              makeAccessibleView(12, 100, true)});

    EXPECT(manager.viewForTag(10).rnFocusable);
    EXPECT(!manager.viewForTag(11).rnFocusable);
    EXPECT(manager.viewForTag(12).rnFocusable);
    // And it shows up in the tree dump, so the cross-host diff can say the prop
    // arrived on all three.
    EXPECT([[root describeTree] containsString:@"focusable"]);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(focus_tab_visits_focusable_views_in_tree_order_and_wraps) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:400];

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 400)
                                                   styleMask:NSWindowStyleMaskBorderless
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.contentView = root;

    basalt::AppKitFocusManager focus(&manager, root);
    mountAll(manager,
             {makeAccessibleView(10, 0, true),
              makeAccessibleView(11, 50, false),
              makeAccessibleView(12, 100, true)});

    // Nothing is focused when a window opens, on either desktop.
    EXPECT_EQ((long)focus.focusedTag(), 0L);

    EXPECT(focus.moveFocus(true));
    EXPECT_EQ((long)focus.focusedTag(), 10L);
    // 11 is not accessible, so Tab goes past it.
    EXPECT(focus.moveFocus(true));
    EXPECT_EQ((long)focus.focusedTag(), 12L);
    // And round again: a window's Tab order wraps.
    EXPECT(focus.moveFocus(true));
    EXPECT_EQ((long)focus.focusedTag(), 10L);
    // Backwards is the same order in reverse.
    EXPECT(focus.moveFocus(false));
    EXPECT_EQ((long)focus.focusedTag(), 12L);

    window.contentView = nil;
    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(focus_activating_nothing_is_not_a_click) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:400];

    NSWindow *window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 400, 400)
                                                   styleMask:NSWindowStyleMaskBorderless
                                                     backing:NSBackingStoreBuffered
                                                       defer:NO];
    window.contentView = root;

    basalt::AppKitFocusManager focus(&manager, root);
    mountAll(manager, {makeAccessibleView(10, 0, true)});

    // Nothing has focus yet, so there is nothing to activate -- and reporting a
    // press with no target is how a keystroke reaches the wrong view.
    EXPECT(!focus.activateFocused());

    focus.moveFocus(true);
    // No emitter is attached to these hand-built shadow views, so the click has
    // nowhere to deliver. It must not crash, and the key is still consumed:
    // that is exactly the shape of Enter arriving between a Remove and its
    // Delete.
    EXPECT(focus.activateFocused());

    window.contentView = nil;
    manager.destroySurfaceRoot(kSurfaceId);
  }
}
