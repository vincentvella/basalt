// The macOS half of core/WindowControl.h.
//
// The one place this differs from the other two in more than spelling is the
// coordinate system. AppKit's origin is the bottom left of the *primary*
// display and y grows upwards; every other desktop, and React Native, put the
// origin at the top left and grow downwards. So `windowBounds` and
// `setWindowPosition` flip, against the primary screen's height, and an app
// that remembers a position and restores it gets the same window back.
//
// Not flipping would be the subtler bug: a window would be restored the same
// distance from the wrong edge, which looks almost right on a large display and
// entirely wrong on a small one.

#include <vector>
#include "WindowControl.h"

#import <Cocoa/Cocoa.h>

namespace basalt {

namespace {


NSWindow *window() {
  return NSApp.keyWindow != nil ? NSApp.keyWindow : NSApp.mainWindow;
}

// The height AppKit measures y from. `NSScreen.screens.firstObject` is the
// primary display, which is the one AppKit's global coordinates are anchored
// to -- not `mainScreen`, which is wherever the key window happens to be.
CGFloat primaryHeight() {
  NSScreen *primary = NSScreen.screens.firstObject;
  return primary != nil ? NSMaxY(primary.frame) : 0.0;
}

} // namespace

std::vector<DisplayInfo> displays() {
  std::vector<DisplayInfo> found;
  @autoreleasepool {
    const CGFloat anchor = primaryHeight();
    for (NSScreen *screen in NSScreen.screens) {
      DisplayInfo info;
      const NSRect frame = screen.frame;
      const NSRect visible = screen.visibleFrame;

      info.x = frame.origin.x;
      // The same flip windowBounds does, and it has to be the same one: a
      // display and a window are compared by apps that place windows, and two
      // conventions in one API would be a bug nobody could see until a second
      // monitor was above the first.
      info.y = anchor - NSMaxY(frame);
      info.width = frame.size.width;
      info.height = frame.size.height;

      // `visibleFrame` is what is left after the menu bar and the Dock, which
      // is exactly what the work area means.
      info.workX = visible.origin.x;
      info.workY = anchor - NSMaxY(visible);
      info.workWidth = visible.size.width;
      info.workHeight = visible.size.height;

      info.scaleFactor = screen.backingScaleFactor > 0.0 ? screen.backingScaleFactor : 1.0;
      // `NSScreen.screens` is documented to put the primary first, and that is
      // also the screen the coordinates above are anchored to -- so this is
      // the same answer primaryHeight() relies on rather than a second one.
      info.primary = found.empty();
      found.push_back(info);
    }
  }
  return found;
}

PointerPosition pointerPosition() {
  PointerPosition where;
  @autoreleasepool {
    // Global, and available whether or not the pointer is over this app --
    // which is the whole point of asking, and is not true on every desktop.
    const NSPoint mouse = NSEvent.mouseLocation;
    where.x = mouse.x;
    where.y = primaryHeight() - mouse.y;
    where.known = true;
  }
  return where;
}

WindowBounds windowBounds() {
  WindowBounds bounds;
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return bounds;
    }
    const NSRect frame = target.frame;
    bounds.x = frame.origin.x;
    // Top-left origin, growing downwards. See the header.
    bounds.y = primaryHeight() - NSMaxY(frame);
    bounds.width = frame.size.width;
    bounds.height = frame.size.height;
    bounds.fullScreen = (target.styleMask & NSWindowStyleMaskFullScreen) != 0;
    bounds.maximized = target.isZoomed;
  }
  return bounds;
}

void setWindowSize(double width, double height) {
  if (width <= 0.0 || height <= 0.0) {
    return;
  }
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    NSRect frame = target.frame;
    // The top edge stays where it is. Resizing a window from its bottom-left
    // origin -- which is what setting the size alone would do -- moves its
    // title bar, and every other desktop grows a window downwards.
    frame.origin.y = NSMaxY(frame) - height;
    frame.size = NSMakeSize(width, height);
    [target setFrame:frame display:YES animate:NO];
  }
}

void setWindowPosition(double x, double y) {
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    // `setFrameTopLeftPoint` takes AppKit coordinates, so the y this was given
    // -- measured down from the top -- is measured back up from the bottom.
    [target setFrameTopLeftPoint:NSMakePoint(x, primaryHeight() - y)];
  }
}

void centerWindow() {
  @autoreleasepool {
    // AppKit's own, which puts the window slightly above centre: that is where
    // Apple's guidelines say a window goes, and an app asking to be centred on
    // a Mac is asking for that rather than for arithmetic.
    [window() center];
  }
}

void setWindowFullScreen(bool fullScreen) {
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    const bool already = (target.styleMask & NSWindowStyleMaskFullScreen) != 0;
    // `toggleFullScreen:` is the only way in, and it toggles -- so asking for
    // the state it is already in has to do nothing rather than reverse it.
    if (already != fullScreen) {
      [target toggleFullScreen:nil];
    }
  }
}

void applyWindowSizeLimits() {
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    const WindowSizeLimits limits = windowSizeLimits();
    // `contentMinSize` and `contentMaxSize` rather than `minSize`/`maxSize`:
    // those are the frame, which includes the title bar, and an app asking for
    // a minimum of 400x300 means 400x300 of its own content -- the same
    // distinction `setWindowSize` already makes.
    target.contentMinSize = NSMakeSize(limits.minWidth > 0.0 ? limits.minWidth : 0.0,
                                       limits.minHeight > 0.0 ? limits.minHeight : 0.0);
    // No maximum is CGFLOAT_MAX rather than zero, which would be a window that
    // cannot be any size at all.
    target.contentMaxSize = NSMakeSize(limits.maxWidth > 0.0 ? limits.maxWidth : CGFLOAT_MAX,
                                       limits.maxHeight > 0.0 ? limits.maxHeight : CGFLOAT_MAX);
  }
}

void setWindowResizable(bool resizable) {
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    // A bit in the style mask, which is also what draws the zoom button: a
    // window that cannot be resized should not offer to be.
    if (resizable) {
      target.styleMask |= NSWindowStyleMaskResizable;
    } else {
      target.styleMask &= ~NSWindowStyleMaskResizable;
    }
  }
}

void setWindowAlwaysOnTop(bool alwaysOnTop) {
  @autoreleasepool {
    NSWindow *target = window();
    if (target == nil) {
      return;
    }
    // Floating, not one of the higher levels. `NSFloatingWindowLevel` puts a
    // window above other applications and still below the things that are
    // supposed to outrank an application -- a screen lock, a system alert -- and
    // asking for more than that is asking to be the bug report.
    target.level = alwaysOnTop ? NSFloatingWindowLevel : NSNormalWindowLevel;
  }
}

WindowCapabilities windowCapabilities() {
  // The one desktop where every one of these is real.
  WindowCapabilities capabilities;
  capabilities.position = true;
  capabilities.minimumSize = true;
  capabilities.maximumSize = true;
  capabilities.resizable = true;
  capabilities.alwaysOnTop = true;
  return capabilities;
}

} // namespace basalt
