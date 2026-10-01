// The macOS view layer, driven by hand-written frames.
//
// The same standing-in-for-Fabric exercise as gtk/demo_layout_gtk.cpp, with the
// same boxes at the same coordinates, so the two platforms can be compared
// directly rather than by eye. Builds and runs without React Native's C++ core,
// which keeps the paint and placement path verifiable on its own.
//
// Three modes. BASALT_DUMP_TREE prints the tree and exits. BASALT_SNAPSHOT=<png>
// renders the hierarchy offscreen and writes it, which is the mode that proves
// anything: the tree dump would look identical whether the flip works or not,
// because it prints the frames that were set rather than where they landed.
// Neither needs a window. Without either, it opens one.

#import "AppKitSnapshot.h"
#import "RnAppKitView.h"

#include <cstdio>
#include <cstdlib>

static RnAppKitView *makeBox(NSInteger tag,
                          CGFloat x,
                          CGFloat y,
                          CGFloat w,
                          CGFloat h,
                          CGFloat r,
                          CGFloat g,
                          CGFloat b,
                          CGFloat a) {
  RnAppKitView *view = [RnAppKitView viewWithTag:tag];
  [view setRnFrameX:x y:y width:w height:h];
  [view setRnBackgroundColorRed:r green:g blue:b alpha:a hasColor:YES];
  return view;
}

static RnAppKitView *buildTree(void) {
  RnAppKitView *root = makeBox(1, 0, 0, 640, 420, 0.12, 0.13, 0.16, 1.0);

  // A row of children, absolutely placed the way Yoga would have resolved them.
  RnAppKitView *a = makeBox(2, 32, 32, 240, 160, 0.30, 0.55, 0.95, 1.0);
  RnAppKitView *b = makeBox(3, 296, 32, 240, 160, 0.95, 0.45, 0.35, 1.0);
  RnAppKitView *c = makeBox(4, 32, 224, 504, 140, 0.35, 0.80, 0.55, 1.0);

  [root insertRnChild:a atIndex:0];
  [root insertRnChild:b atIndex:1];
  [root insertRnChild:c atIndex:2];

  // Nested child, to prove coordinates are parent-relative. On a flipped view
  // this lands 24pt below `a`'s top edge; on an unflipped one it would land
  // 24pt above its bottom, which is the bug the flip exists to prevent.
  RnAppKitView *nested = makeBox(5, 24, 24, 120, 90, 1.0, 1.0, 1.0, 0.85);
  [a insertRnChild:nested atIndex:0];

  // Half-opacity child, to prove the opacity layer. Distinct from the alpha
  // above: that one is a translucent colour, this one is a translucent view.
  RnAppKitView *faded = makeBox(6, 24, 24, 160, 90, 0.1, 0.1, 0.1, 1.0);
  [faded setRnOpacity:0.4];
  [c insertRnChild:faded atIndex:0];

  return root;
}

int main(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    RnAppKitView *root = buildTree();

    if (getenv("BASALT_DUMP_TREE") != nullptr) {
      std::fputs([[root describeTree] UTF8String], stdout);
      return 0;
    }

    if (const char *snapshotPath = getenv("BASALT_SNAPSHOT")) {
      return RnAppKitWriteSnapshot(root, [NSString stringWithUTF8String:snapshotPath]) ? 0 : 1;
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    NSWindow *window =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 420)
                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    window.title = @"basalt-core — macOS mounting skeleton";
    window.contentView = root;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
  }
  return 0;
}
