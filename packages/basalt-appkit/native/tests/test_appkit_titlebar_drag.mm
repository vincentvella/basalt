// Which parts of an app-drawn header drag the window, on macOS.
//
// The same four questions `basalt-gtk/native/tests/test_titlebar_drag.cpp`
// asks of GTK, in the same order, because the rule is shared and the answers
// are supposed to be identical: core/TitleBarRegions.h states it once and
// three hosts apply it. Until this file macOS applied it nowhere, so a hidden
// title bar there was a header that looked right and could not move its
// window. The website said so in a caution box, which is the wrong place for
// it to be true.
//
// No React Native and no window: a view tree and a point, which is all
// `isTitleBarDragRegionAt` takes.

#include "TestHarness.h"

#import "AppKitTitleBarLayout.h"
#import "RnAppKitView.h"

#include "TitleBarRegions.h"

namespace {

RnAppKitView *box(NSInteger tag, CGFloat x, CGFloat y, CGFloat w, CGFloat h) {
  RnAppKitView *view = [RnAppKitView viewWithTag:tag];
  [view setRnFrameX:x y:y width:w height:h];
  return view;
}

RnAppKitView *marked(NSInteger tag,
                     CGFloat x,
                     CGFloat y,
                     CGFloat w,
                     CGFloat h,
                     const char *nativeId) {
  RnAppKitView *view = box(tag, x, y, w, h);
  view.rnNativeId = @(nativeId);
  return view;
}

} // namespace

TEST(appkit_titlebar_a_label_inside_a_drag_region_drags_the_window) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 800, 600);
    RnAppKitView *header = marked(2, 0, 0, 800, 32, basalt::kTitleBarDragRegionId);
    [root insertRnChild:header atIndex:0];
    // Unmarked, and inside the header: the marked ancestor decides.
    [header insertRnChild:box(3, 10, 5, 100, 20) atIndex:0];

    EXPECT(basalt::isTitleBarDragRegionAt(root, 20.0, 10.0));
    EXPECT(basalt::isTitleBarDragRegionAt(root, 400.0, 10.0));
  }
}

TEST(appkit_titlebar_a_no_drag_region_inside_a_drag_region_stays_pressable) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 800, 600);
    RnAppKitView *header = marked(2, 0, 0, 800, 32, basalt::kTitleBarDragRegionId);
    [root insertRnChild:header atIndex:0];
    RnAppKitView *button = marked(3, 600, 0, 50, 32, basalt::kTitleBarNoDragRegionId);
    [header insertRnChild:button atIndex:0];

    EXPECT(!basalt::isTitleBarDragRegionAt(root, 620.0, 10.0));

    // And a drag region nested inside that button drags again: the nearest
    // marker decides, not the outermost.
    [button insertRnChild:marked(4, 5, 5, 10, 10, basalt::kTitleBarDragRegionId) atIndex:0];
    EXPECT(basalt::isTitleBarDragRegionAt(root, 607.0, 7.0));
  }
}

TEST(appkit_titlebar_a_point_under_nothing_marked_is_ordinary_client_area) {
  @autoreleasepool {
    RnAppKitView *root = box(1, 0, 0, 800, 600);
    [root insertRnChild:marked(2, 0, 0, 800, 32, basalt::kTitleBarDragRegionId) atIndex:0];
    [root insertRnChild:box(3, 0, 32, 800, 568) atIndex:1];

    EXPECT(!basalt::isTitleBarDragRegionAt(root, 400.0, 300.0));
    // A miss altogether -- outside the root -- is not a drag either.
    EXPECT(!basalt::isTitleBarDragRegionAt(root, 900.0, 10.0));
  }
}

TEST(appkit_titlebar_a_root_marked_whole_drags_everywhere_it_is_not_overridden) {
  @autoreleasepool {
    // An app that wants its entire surface to drag, which is a real shape for
    // a small tool window, and the case that fails if the walk stops before
    // the root rather than after it.
    RnAppKitView *root = box(1, 0, 0, 400, 300);
    root.rnNativeId = @(basalt::kTitleBarDragRegionId);
    [root insertRnChild:marked(2, 100, 100, 80, 40, basalt::kTitleBarNoDragRegionId) atIndex:0];

    EXPECT(basalt::isTitleBarDragRegionAt(root, 10.0, 10.0));
    EXPECT(!basalt::isTitleBarDragRegionAt(root, 140.0, 120.0));
  }
}

TEST(appkit_titlebar_nothing_drags_without_a_tree) {
  // The dispatcher asks before it knows there is a surface, and a nil root is
  // an ordinary press rather than a crash.
  EXPECT(!basalt::isTitleBarDragRegionAt(nil, 10.0, 10.0));
}
