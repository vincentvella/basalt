// Tests for the macOS view layer.
//
// RnAppKitView has no React Native dependency, so these need no Fabric machinery
// and no window. They share the toolkit-free harness the GTK tests use, which
// is the first small piece of evidence that a second platform can reuse what is
// here rather than reimplement it.

#include "TestHarness.h"

#import "RnAppKitView.h"

#include <sstream>

namespace {

// Components of a layer's background colour, or all -1 if it has none.
struct Background {
  CGFloat r = -1, g = -1, b = -1, a = -1;
  bool present = false;
};

Background backgroundOf(RnAppKitView *view) {
  Background result;
  CGColorRef color = view.layer.backgroundColor;
  if (color == nullptr) {
    return result;
  }
  const CGFloat *components = CGColorGetComponents(color);
  result.present = true;
  result.r = components[0];
  result.g = components[1];
  result.b = components[2];
  result.a = components[3];
  return result;
}

RnAppKitView *box(NSInteger tag, CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
  RnAppKitView *view = [RnAppKitView viewWithTag:tag];
  [view setRnFrameX:x y:y width:w height:h];
  return view;
}

NSInteger tagOfSubview(RnAppKitView *parent, NSUInteger index) {
  return ((RnAppKitView *)parent.subviews[index]).rnTag;
}

} // namespace

TEST(appkit_view_stores_its_tag_and_frame) {
  @autoreleasepool {
    RnAppKitView *view = box(7, 1.5, 2.5, 30, 40);

    EXPECT_EQ((long)view.rnTag, 7L);
    EXPECT_NEAR(view.frame.origin.x, 1.5, 0.001);
    EXPECT_NEAR(view.frame.origin.y, 2.5, 0.001);
    EXPECT_NEAR(view.frame.size.width, 30.0, 0.001);
    EXPECT_NEAR(view.frame.size.height, 40.0, 0.001);
  }
}

// The reason the view exists at all. AppKit's origin is bottom-left; every
// frame Fabric produces assumes top-left, so a child at y=32 in a 420-tall
// parent must sit 32 from the top and not 32 from the bottom.
TEST(appkit_view_is_flipped_so_frames_are_top_left) {
  @autoreleasepool {
    RnAppKitView *parent = box(1, 0, 0, 640, 420);
    RnAppKitView *child = box(2, 32, 32, 240, 160);
    [parent insertRnChild:child atIndex:0];

    EXPECT(parent.isFlipped);

    // The child's own origin, expressed in the parent. Unflipped this would be
    // 228 -- 420 - 32 - 160 -- which is the bug in numbers.
    const NSPoint origin = [child convertPoint:NSZeroPoint toView:parent];
    EXPECT_NEAR(origin.x, 32.0, 0.001);
    EXPECT_NEAR(origin.y, 32.0, 0.001);
  }
}

TEST(appkit_view_children_insert_at_the_requested_index) {
  @autoreleasepool {
    RnAppKitView *parent = box(1, 0, 0, 100, 100);
    RnAppKitView *a = box(10, 0, 0, 10, 10);
    RnAppKitView *b = box(11, 0, 0, 10, 10);
    RnAppKitView *c = box(12, 0, 0, 10, 10);

    [parent insertRnChild:a atIndex:0];
    [parent insertRnChild:b atIndex:1];
    // Into the middle, which is the case an append-only implementation passes
    // every other test without handling.
    [parent insertRnChild:c atIndex:1];

    EXPECT_EQ((long)parent.subviews.count, 3L);
    EXPECT_EQ((long)tagOfSubview(parent, 0), 10L);
    EXPECT_EQ((long)tagOfSubview(parent, 1), 12L);
    EXPECT_EQ((long)tagOfSubview(parent, 2), 11L);

    [parent removeRnChild:c];
    EXPECT_EQ((long)parent.subviews.count, 2L);
    EXPECT_EQ((long)tagOfSubview(parent, 0), 10L);
    EXPECT_EQ((long)tagOfSubview(parent, 1), 11L);

    // Removed, not destroyed: a reinsertion elsewhere has to still work.
    EXPECT(c.superview == nil);
    RnAppKitView *other = box(2, 0, 0, 100, 100);
    [other insertRnChild:c atIndex:0];
    EXPECT_EQ((long)tagOfSubview(other, 0), 12L);
  }
}

// Removing a view that is not ours must not detach it from whoever owns it.
TEST(appkit_view_removes_only_its_own_child) {
  @autoreleasepool {
    RnAppKitView *owner = box(1, 0, 0, 100, 100);
    RnAppKitView *stranger = box(2, 0, 0, 100, 100);
    RnAppKitView *child = box(10, 0, 0, 10, 10);

    [owner insertRnChild:child atIndex:0];
    [stranger removeRnChild:child];

    EXPECT_EQ((long)owner.subviews.count, 1L);
    EXPECT(child.superview == owner);
  }
}

TEST(appkit_view_background_reaches_the_layer) {
  @autoreleasepool {
    RnAppKitView *view = box(1, 0, 0, 10, 10);
    EXPECT(!backgroundOf(view).present);

    [view setRnBackgroundColorRed:0.25 green:0.5 blue:0.75 alpha:0.5 hasColor:YES];
    const Background set = backgroundOf(view);
    EXPECT(set.present);
    EXPECT_NEAR(set.r, 0.25, 0.001);
    EXPECT_NEAR(set.g, 0.5, 0.001);
    EXPECT_NEAR(set.b, 0.75, 0.001);
    EXPECT_NEAR(set.a, 0.5, 0.001);

    // sRGB, not the display's space. The same colour has to come out the same
    // on a wide-gamut Mac as it does on Linux, or "consistent across platforms"
    // stops being true in the one place nobody thinks to check.
    CGColorSpaceRef space = CGColorGetColorSpace(view.layer.backgroundColor);
    CFStringRef name = CGColorSpaceCopyName(space);
    EXPECT(name != nullptr && CFEqual(name, kCGColorSpaceSRGB));
    if (name != nullptr) {
      CFRelease(name);
    }

    // No background is not transparent black: the layer must have no colour at
    // all, so whatever is behind it shows through unmodified.
    [view setRnBackgroundColorRed:0.25 green:0.5 blue:0.75 alpha:0.5 hasColor:NO];
    EXPECT(!backgroundOf(view).present);
  }
}

TEST(appkit_view_opacity_clipping_and_radius_reach_the_layer) {
  @autoreleasepool {
    RnAppKitView *view = box(1, 0, 0, 10, 10);

    EXPECT_NEAR(view.layer.opacity, 1.0, 0.001);
    EXPECT(!view.layer.masksToBounds);
    EXPECT_NEAR(view.layer.cornerRadius, 0.0, 0.001);

    [view setRnOpacity:0.4];
    [view setRnClipsChildren:YES];
    [view setRnCornerRadius:8];

    EXPECT_NEAR(view.layer.opacity, 0.4, 0.001);
    EXPECT(view.layer.masksToBounds);
    EXPECT_NEAR(view.layer.cornerRadius, 8.0, 0.001);
  }
}

// A scroll offset moves the children and leaves their frames alone. Through
// bounds.origin, so AppKit shifts drawing, clipping and hit testing together --
// an implementation that moved each child instead would have to undo it on
// every mutation, and would fight the frames Yoga produced.
TEST(appkit_scroll_offset_moves_children_without_moving_their_frames) {
  @autoreleasepool {
    RnAppKitView *page = box(99, 0, 0, 400, 400);
    RnAppKitView *scroller = box(1, 0, 0, 200, 100);
    RnAppKitView *content = box(2, 0, 0, 200, 600);
    [page insertRnChild:scroller atIndex:0];
    [scroller insertRnChild:content atIndex:0];

    // Measured against the *page*, not the scroller. A view's own coordinate
    // system is its bounds space, so converting into the scroller would report
    // the frame back unchanged however far it had scrolled -- which is exactly
    // the wrong question, and the first version of this test asked it.
    EXPECT_NEAR(scroller.rnScrollOffset.y, 0.0, 0.001);
    EXPECT_NEAR([content convertPoint:NSZeroPoint toView:page].y, 0.0, 0.001);

    [scroller setRnScrollOffsetX:0 y:150];

    EXPECT_NEAR(scroller.rnScrollOffset.y, 150.0, 0.001);
    // The frame is untouched: it is still what the shadow tree assigned.
    EXPECT_NEAR(content.frame.origin.y, 0.0, 0.001);
    // But it now sits 150 above the scroller's visible top.
    EXPECT_NEAR([content convertPoint:NSZeroPoint toView:page].y, -150.0, 0.001);
  }
}

TEST(appkit_scroll_offset_is_reported_in_the_tree) {
  @autoreleasepool {
    RnAppKitView *scroller = box(1, 0, 0, 200, 100);
    [scroller setRnScrollOffsetX:0 y:40];

    const std::string described = [scroller describeTree].UTF8String;
    // The same `scroll=(x,y)` the GTK side emits, so a scrolled list shows up
    // in the cross-platform diff.
    EXPECT(described.find("scroll=(0,40)") != std::string::npos);
  }
}

// The same format the GTK side emits, character for character, so the two
// platforms can be compared without reading two renderers.
TEST(appkit_view_describes_its_tree_like_the_gtk_one) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 640, 420);
    [root setRnBackgroundColorRed:0.12 green:0.13 blue:0.16 alpha:1.0 hasColor:YES];

    RnAppKitView *child = box(2, 32, 32, 240, 160);
    [child setRnOpacity:0.4];
    [child setRnClipsChildren:YES];
    [root insertRnChild:child atIndex:0];

    const std::string described = [root describeTree].UTF8String;
    const std::string expected =
        "view tag=1 frame=(0,0 640x420) bg=#1f2129ff\n"
        "  view tag=2 frame=(32,32 240x160) opacity=0.4 clip\n";
    EXPECT_EQ(described, expected);
  }
}

int main(int argc, char **argv) {
  return basalt::testing::runAllTests(argc, argv);
}
