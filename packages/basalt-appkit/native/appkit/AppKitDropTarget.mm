#import "AppKitDropTarget.h"

#include "DragAndDrop.h"

#import <AppKit/AppKit.h>

#include <string>

namespace basalt {

facebook::react::Tag dropTargetAt(RnAppKitView *root, double x, double y, std::uint16_t accepts) {
  if (root == nil) {
    return 0;
  }
  // `RnAppKitHitTest`, not NSView's `hitTest:`. The platform's own is what
  // touches use -- it honours pointer-events, clipping, transforms and z
  // order the way this renderer means them -- and AppKit's stops at the first
  // subview whose own hitTest: answers, which for these views is the page
  // rather than the target inside it. Using the wrong one found a view two
  // steps below the root and no marker anywhere above it.
  //
  // From there the walk up is ours, for the reason core/DragAndDrop.h gives:
  // a label inside a drop target should not need marking.
  RnAppKitView *hit = RnAppKitHitTest(root, x, y);
  for (NSView *view = hit; view != nil; view = view.superview) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      auto *candidate = (RnAppKitView *)view;
      NSString *identifier = candidate.rnNativeId;
      if (identifier != nil) {
        const std::uint16_t wanted = dropAcceptsFrom(identifier.UTF8String);
        if (wanted != DropAcceptsNone && (wanted & accepts) != 0) {
          return (facebook::react::Tag)candidate.rnTag;
        }
      }
    }
    if (view == root) {
      break;
    }
  }
  return 0;
}

} // namespace basalt

// What AppKit asks about the drag while it is in flight.
//
// A class of its own because RnAppKitView does not conform to
// NSDraggingSource, and should not: a view is a drag source only while a drag
// it started is running, and that is a session's lifetime rather than a
// view's.
@interface RnDragSource : NSObject <NSDraggingSource>
@end

@implementation RnDragSource

- (NSDragOperation)draggingSession:(NSDraggingSession *)session
    sourceOperationMaskForDraggingContext:(NSDraggingContext)context {
  (void)session;
  // Copy in both contexts. This platform has no notion of a drag that moves
  // or links, and offering one it cannot honour would be a cursor promising
  // something the drop does not do -- the same rule the drop side applies.
  (void)context;
  return NSDragOperationCopy;
}

@end

namespace basalt {
namespace {

// Alive for as long as the session is. AppKit holds the source weakly, and a
// source collected mid-drag is a drag that stops answering.
RnDragSource *gDragSource = nil;

} // namespace

DragPayload dragPayloadAt(RnAppKitView *root, double x, double y) {
  if (root == nil) {
    return {};
  }
  RnAppKitView *hit = RnAppKitHitTest(root, x, y);
  // The same walk the drop side makes: a label inside a draggable view should
  // be draggable without being marked itself.
  for (NSView *view = hit; view != nil; view = view.superview) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      NSString *identifier = ((RnAppKitView *)view).rnNativeId;
      if (identifier != nil) {
        const DragPayload payload = dragPayloadFrom(identifier.UTF8String);
        if (!payload.empty()) {
          return payload;
        }
      }
    }
    if (view == root) {
      break;
    }
  }
  return {};
}

bool beginDragIfMarked(RnAppKitView *root, double x, double y) {
  const DragPayload payload = dragPayloadAt(root, x, y);
  if (payload.empty()) {
    return false;
  }

  // What the rest of the desktop will receive. A file URL for a file and a
  // string for text, which are the two every other application understands.
  id<NSPasteboardWriting> item = nil;
  if (payload.hasFiles()) {
    item = [NSURL fileURLWithPath:[NSString stringWithUTF8String:payload.files.front().c_str()]];
  } else {
    item = [NSString stringWithUTF8String:payload.text.c_str()];
  }

  NSDraggingItem *dragged =
      [[NSDraggingItem alloc] initWithPasteboardWriter:item];
  // Something to see while dragging. A plain rectangle where the view is,
  // rather than a snapshot: a snapshot means rendering the view again into an
  // image, and the drag has to start on this event or not at all.
  const NSRect frame = NSMakeRect(x - 40, y - 20, 80, 40);
  [dragged setDraggingFrame:frame contents:nil];

  NSEvent *event = NSApp.currentEvent;
  if (event == nil) {
    return false;
  }
  if (gDragSource == nil) {
    gDragSource = [[RnDragSource alloc] init];
  }
  [root beginDraggingSessionWithItems:@[ dragged ] event:event source:gDragSource];
  return true;
}

} // namespace basalt

// The dragging destination itself.
//
// On a view of its own, layered over the surface root, rather than on
// RnAppKitView: every RnAppKitView would otherwise inherit the protocol and
// AppKit would ask the deepest one, which is the opposite of the rule -- the
// deepest *marked* view decides, and most views are not marked.
@interface RnDropTargetView : NSView
@property(nonatomic, weak, nullable) RnAppKitView *rnRoot;
@property(nonatomic) facebook::react::Tag rnCurrent;
@end

@implementation RnDropTargetView

// Not drawn and not hit-testable for anything but drags: a view that swallowed
// clicks would break every press in the app.
- (NSView *)hitTest:(NSPoint)point {
  (void)point;
  return nil;
}

- (std::uint16_t)rnOfferedBy:(id<NSDraggingInfo>)info {
  std::uint16_t offered = basalt::DropAcceptsNone;
  NSPasteboard *board = info.draggingPasteboard;
  if ([board canReadObjectForClasses:@[ [NSURL class] ]
                             options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}]) {
    offered |= basalt::DropAcceptsFiles;
  }
  if ([board canReadObjectForClasses:@[ [NSString class] ] options:@{}]) {
    offered |= basalt::DropAcceptsText;
  }
  return offered;
}

// The point a drag is over, in the root's coordinates.
- (NSPoint)rnPointFor:(id<NSDraggingInfo>)info {
  RnAppKitView *root = self.rnRoot;
  if (root == nil) {
    return NSZeroPoint;
  }
  return [root convertPoint:info.draggingLocation fromView:nil];
}

- (void)rnReportLeaveAt:(NSPoint)point {
  if (self.rnCurrent == 0) {
    return;
  }
  basalt::DropEvent event;
  event.tag = self.rnCurrent;
  event.phase = basalt::DropPhase::Leave;
  event.x = point.x;
  event.y = point.y;
  basalt::reportDrop(event);
  self.rnCurrent = 0;
}

- (NSDragOperation)rnUpdate:(id<NSDraggingInfo>)info {
  const NSPoint point = [self rnPointFor:info];
  const facebook::react::Tag found =
      basalt::dropTargetAt(self.rnRoot, point.x, point.y, [self rnOfferedBy:info]);

  if (found != self.rnCurrent) {
    [self rnReportLeaveAt:point];
  }
  if (found == 0) {
    // Nothing here takes it, which is what makes the cursor say so.
    return NSDragOperationNone;
  }
  self.rnCurrent = found;

  basalt::DropEvent event;
  event.tag = found;
  event.phase = basalt::DropPhase::Over;
  event.x = point.x;
  event.y = point.y;
  basalt::reportDrop(event);
  return NSDragOperationCopy;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
  return [self rnUpdate:sender];
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
  return [self rnUpdate:sender];
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
  [self rnReportLeaveAt:sender != nil ? [self rnPointFor:sender] : NSZeroPoint];
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
  NSPasteboard *board = sender.draggingPasteboard;

  basalt::DragPayload payload;
  NSArray<NSURL *> *urls =
      [board readObjectsForClasses:@[ [NSURL class] ]
                           options:@{NSPasteboardURLReadingFileURLsOnlyKey : @YES}];
  for (NSURL *url in urls) {
    if (url.isFileURL && url.path != nil) {
      // A path, not a URL: core/DragAndDrop.h says every host hands back
      // something path-shaped, and an app that wanted a URL can make one.
      payload.files.emplace_back(url.path.UTF8String);
    }
  }
  NSString *text = [board stringForType:NSPasteboardTypeString];
  if (text != nil) {
    payload.text = text.UTF8String;
  }

  // Asked again from the contents rather than from what the pasteboard could
  // read, for the reason the GTK host gives: this is the moment the payload is
  // known.
  std::uint16_t carries = basalt::DropAcceptsNone;
  if (payload.hasFiles()) {
    carries |= basalt::DropAcceptsFiles;
  }
  if (payload.hasText()) {
    carries |= basalt::DropAcceptsText;
  }

  const NSPoint point = [self rnPointFor:sender];
  const facebook::react::Tag found =
      basalt::dropTargetAt(self.rnRoot, point.x, point.y, carries);
  self.rnCurrent = 0;
  if (found == 0) {
    return NO;
  }

  basalt::DropEvent event;
  event.tag = found;
  event.phase = basalt::DropPhase::Drop;
  event.x = point.x;
  event.y = point.y;
  event.payload = payload;
  basalt::reportDrop(event);
  return YES;
}

@end

namespace basalt {

void attachDropTarget(RnAppKitView *root) {
  if (root == nil) {
    return;
  }
  RnDropTargetView *target = [[RnDropTargetView alloc] initWithFrame:root.bounds];
  target.rnRoot = root;
  target.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
  [target registerForDraggedTypes:@[ NSPasteboardTypeFileURL, NSPasteboardTypeString ]];
  // Above everything, so no app view can shadow the destination -- it is not
  // hit-testable for clicks, so being on top costs nothing.
  [root addSubview:target positioned:NSWindowAbove relativeTo:nil];
}

} // namespace basalt
