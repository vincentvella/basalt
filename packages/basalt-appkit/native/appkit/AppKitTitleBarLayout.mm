#import "AppKitTitleBarLayout.h"

#include "TitleBarRegions.h"

#import <AppKit/AppKit.h>

#include <string>

namespace basalt {

bool isTitleBarDragRegionAt(RnAppKitView *root, double x, double y) {
  if (root == nil) {
    return false;
  }
  RnAppKitView *hit = RnAppKitHitTest(root, x, y);
  for (NSView *view = hit; view != nil; view = view.superview) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      NSString *identifier = ((RnAppKitView *)view).rnNativeId;
      if (identifier != nil) {
        const std::string id = identifier.UTF8String;
        if (id == kTitleBarDragRegionId) {
          return true;
        }
        if (id == kTitleBarNoDragRegionId) {
          return false;
        }
      }
    }
    // After the test rather than before it, so a root marked whole still
    // answers for a point inside it. Stopping first is the bug the fourth
    // test in test_appkit_titlebar_drag.mm exists to catch.
    if (view == root) {
      break;
    }
  }
  return false;
}

} // namespace basalt
