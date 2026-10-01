#import "AppKitSnapshot.h"

#import <QuartzCore/QuartzCore.h>

#include <cstdio>

// Two things here are not obvious and cost an afternoon each if guessed at.
//
// The hierarchy needs a window, and the window needs to have displayed once.
// AppKit does not attach a subview's layer to its superview's layer when the
// subview is added -- it assembles the layer tree during a display cycle, and a
// view with no window never has one. Without this the snapshot comes out as the
// root's background colour and nothing else, which reads as "the children did
// not paint" rather than "the children are not in the layer tree". The window is
// never ordered front, so nothing appears on screen.
//
// The render goes through the layer tree rather than `cacheDisplayInRect:`,
// because these views draw nothing themselves: every prop the view layer
// supports is a CALayer property, so `renderInContext:` is what actually
// exercises the code under test.
bool RnAppKitWriteSnapshot(RnAppKitView *root, NSString *path) {
  // AppKit has to be woken up before it will render: a view outside a running
  // application draws nothing, and does so silently.
  [NSApplication sharedApplication];

  const NSRect bounds = root.bounds;
  const NSInteger width = (NSInteger)bounds.size.width;
  const NSInteger height = (NSInteger)bounds.size.height;
  if (width <= 0 || height <= 0) {
    std::fputs("nothing to snapshot: the root has no size\n", stderr);
    return false;
  }

  // A view that is already in a window has a layer tree; one that is not needs
  // a window made for it. Checking rather than always making one matters: a
  // live host's root is the window's contentView, and putting it into a second
  // window would take it out of the first.
  NSWindow *offscreen = nil;
  if (root.window != nil) {
    [root.window displayIfNeeded];
  } else {
    offscreen = [[NSWindow alloc] initWithContentRect:bounds
                                            styleMask:NSWindowStyleMaskBorderless
                                              backing:NSBackingStoreBuffered
                                                defer:NO];
    offscreen.contentView = root;
    [offscreen display];
  }

  CGColorSpaceRef colorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr,
                                               (size_t)width,
                                               (size_t)height,
                                               8,
                                               0,
                                               colorSpace,
                                               kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(colorSpace);
  if (context == nullptr) {
    std::fputs("could not create a bitmap context\n", stderr);
    return false;
  }

  // Core Graphics' origin is bottom-left and the layer tree's, having come from
  // a flipped view, is top-left. Flipping the context puts them back in
  // agreement; without it the image comes out mirrored vertically, which is the
  // same class of bug `isFlipped` exists to prevent one level up.
  CGContextTranslateCTM(context, 0, (CGFloat)height);
  CGContextScaleCTM(context, 1, -1);

  [root.layer renderInContext:context];

  CGImageRef image = CGBitmapContextCreateImage(context);
  CGContextRelease(context);
  if (image == nullptr) {
    return false;
  }

  NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithCGImage:image];
  CGImageRelease(image);
  NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];

  // The root is handed back, because a caller that snapshots twice needs it out
  // of the window it was just put in -- a view has one superview, and leaving it
  // parented here would make the second snapshot's window silently steal it.
  if (offscreen != nil) {
    offscreen.contentView = [[NSView alloc] initWithFrame:bounds];
  }

  return [png writeToFile:path atomically:YES];
}
