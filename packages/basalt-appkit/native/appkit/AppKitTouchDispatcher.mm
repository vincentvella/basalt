#import "AppKitTouchDispatcher.h"

#import "AppKitDropTarget.h"

#include "Gestures.h"

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/components/view/TouchEvent.h>

#include <cstdint>

// The bridge between AppKit's protocol and the C++ dispatcher. A C++ object
// cannot conform to an Objective-C protocol, and the root holds its handler
// weakly, so this exists and the dispatcher owns it.
@interface RnAppKitInputTarget : NSObject <RnAppKitInputHandler> {
  // Whether this press has already been offered as a drag. See
  // rnMouseDraggedTo:.
  BOOL _dragAsked;
}
@property(nonatomic, assign) basalt::AppKitTouchDispatcher *dispatcher;
@end

@implementation RnAppKitInputTarget

- (void)rnMouseDownAt:(NSPoint)point button:(int)button {
  // A new press: the drag question is open again.
  _dragAsked = NO;
  if (_dispatcher != nullptr) {
    _dispatcher->dispatchTouchStart(point.x, point.y, static_cast<basalt::PointerButton>(button));
  }
}

- (void)rnMouseDraggedTo:(NSPoint)point {
  if (_dispatcher == nullptr) {
    return;
  }
  // A drag out of the window, if the press started on a marked view. Asked
  // here rather than on mouseDown: a press is not a drag until it moves, and
  // asking on the way down would turn every press on a draggable view into
  // one.
  //
  // Answered once per press: `beginDraggingSessionWithItems:` takes the
  // gesture, and AppKit runs its own loop until the drop -- so the touch that
  // started it never ends, and it is cancelled rather than left hanging.
  if (!_dragAsked) {
    _dragAsked = YES;
    if (_dispatcher->beginDragOut(point.x, point.y)) {
      _dispatcher->dispatchTouchCancel();
      return;
    }
  }
  _dispatcher->dispatchTouchMove(point.x, point.y);
}

- (void)rnMouseUpAt:(NSPoint)point button:(int)button {
  if (_dispatcher != nullptr) {
    _dispatcher->dispatchTouchEnd(point.x, point.y, static_cast<basalt::PointerButton>(button));
  }
}

- (void)rnMouseMovedTo:(NSPoint)point {
  if (_dispatcher != nullptr) {
    _dispatcher->dispatchHover(point.x, point.y);
  }
}

- (void)rnMouseExited {
  if (_dispatcher != nullptr) {
    _dispatcher->dispatchHoverLeave();
  }
}

@end

namespace basalt {

using facebook::react::HighResTimeStamp;
using facebook::react::Point;
using facebook::react::PointerEvent;
using facebook::react::Tag;
using facebook::react::Touch;
using facebook::react::TouchEvent;
using facebook::react::TouchEventEmitter;
using facebook::react::Touches;

namespace {

// A desktop pointer is one touch point, and React Native identifies touches by
// number. Zero is the first (and here only) finger. The same number identifies
// the pointer in pointer events, where React Native keys its hover tracking by
// it -- and where zero is what React Native's own iOS host uses for a mouse.
constexpr int kPointerIdentifier = 0;

// The payload for a hovering pointer, at a point in root coordinates with the
// target's origin in the same space.
PointerEvent hoverEvent(double x, double y, double originX, double originY) {
  PointerEvent event{};
  event.pointerId = kPointerIdentifier;
  event.pointerType = "mouse";
  event.clientPoint = Point{.x = static_cast<facebook::react::Float>(x),
                            .y = static_cast<facebook::react::Float>(y)};
  event.screenPoint = event.clientPoint;
  event.offsetPoint = Point{.x = static_cast<facebook::react::Float>(x - originX),
                            .y = static_cast<facebook::react::Float>(y - originY)};
  event.width = 1;
  event.height = 1;
  // A hovering mouse presses nothing. `button` is -1 rather than 0 because 0 is
  // the left button; -1 is W3C's "no button changed state".
  event.button = -1;
  event.buttons = 0;
  event.isPrimary = true;
  event.timeStamp = HighResTimeStamp::now();
  return event;
}

} // namespace

AppKitTouchDispatcher::AppKitTouchDispatcher(AppKitMountingManager *mountingManager,
                                             RnAppKitView *surfaceRoot)
    : mountingManager_(mountingManager), surfaceRoot_(surfaceRoot) {
  RnAppKitInputTarget *target = [[RnAppKitInputTarget alloc] init];
  target.dispatcher = this;
  inputTarget_ = target;
  surfaceRoot_.rnInputHandler = target;
  // The root's tracking area is only wanted once there is somewhere to send
  // hover, and AppKit will not ask again just because a property changed.
  [surfaceRoot_ updateTrackingAreas];
}

AppKitTouchDispatcher::~AppKitTouchDispatcher() {
  // The root's reference is weak, so it goes nil on its own; clearing the back
  // pointer first means an event already in flight cannot reach a dead object.
  ((RnAppKitInputTarget *)inputTarget_).dispatcher = nullptr;
  inputTarget_ = nil;
}

void AppKitTouchDispatcher::synthesiseTap(double x, double y) {
  synthesiseTap(x, y, basalt::PointerButton::Primary);
}

void AppKitTouchDispatcher::synthesiseTap(double x, double y, basalt::PointerButton button) {
  dispatchTouchStart(x, y, button);
  dispatchTouchEnd(x, y, button);
}

void AppKitTouchDispatcher::synthesiseHover(double x, double y) {
  if (x < 0 || y < 0) {
    dispatchHoverLeave();
    return;
  }
  dispatchHover(x, y);
}

void AppKitTouchDispatcher::synthesiseDrag(double fromX, double fromY, double toX, double toY, int steps) {
  dispatchTouchStart(fromX, fromY, basalt::PointerButton::Primary);
  const int count = steps < 1 ? 1 : steps;
  for (int step = 1; step <= count; step++) {
    const double progress = static_cast<double>(step) / count;
    dispatchTouchMove(fromX + (toX - fromX) * progress, fromY + (toY - fromY) * progress);
  }
  dispatchTouchEnd(toX, toY, basalt::PointerButton::Primary);
}

// ---------------------------------------------------------------------------
// Hit testing
// ---------------------------------------------------------------------------

bool AppKitTouchDispatcher::beginDragOut(double x, double y) {
  return beginDragIfMarked(surfaceRoot_, x, y);
}

Tag hitTestTag(RnAppKitView *root, double x, double y) {
  RnAppKitView *hit = RnAppKitHitTest(root, x, y);
  return hit == nil ? 0 : static_cast<Tag>(hit.rnTag);
}

namespace {

// The React Native views under a point, innermost first, each with its origin
// in the root's coordinates. That is what a gesture recogniser needs and a
// touch does not: a touch is reported against one target, while a gesture may
// be attached to any ancestor of the view that was hit.
//
// Built only when something is attached; see core/Gestures.h.
std::vector<basalt::HitView> hitChain(RnAppKitView *root, double x, double y) {
  std::vector<basalt::HitView> chain;
  RnAppKitView *hit = RnAppKitHitTest(root, x, y);

  for (NSView *view = hit; view != nil; view = view.superview) {
    if (![view isKindOfClass:[RnAppKitView class]]) {
      continue;
    }
    // The view's own origin in root coordinates. Conversion rather than summed
    // frames, because a scrolled ancestor's bounds origin has to count and
    // AppKit already knows how.
    const NSPoint origin = [view convertPoint:NSZeroPoint toView:root];
    chain.push_back(basalt::HitView{.tag = static_cast<int>(((RnAppKitView *)view).rnTag),
                                    .originX = origin.x,
                                    .originY = origin.y});
    if (view == root) {
      break;
    }
  }
  return chain;
}

} // namespace

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

// The innermost view under a point, and where inside it that point is. The
// pointer event wants both whichever button it was; PointerEventsProcessor
// walks up from there itself.
static Tag targetUnder(RnAppKitView *surfaceRoot, double x, double y, NSPoint *origin) {
  RnAppKitView *hit = RnAppKitHitTest(surfaceRoot, x, y);
  for (NSView *view = hit; view != nil; view = view.superview) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      *origin = [view convertPoint:NSZeroPoint toView:surfaceRoot];
      return static_cast<Tag>(((RnAppKitView *)view).rnTag);
    }
    if (view == surfaceRoot) {
      break;
    }
  }
  return 0;
}

void AppKitTouchDispatcher::dispatchTouchStart(double x, double y, basalt::PointerButton button) {
  NSPoint origin = NSZeroPoint;
  const Tag target = targetUnder(surfaceRoot_, x, y, &origin);
  if (target == 0) {
    return;
  }

  // Every button produces a pointer event, which is where `button` can be said
  // at all.
  emitPointerButton(true, target, origin.x, origin.y, x, y, button);

  // Only the primary one goes any further. A secondary click is not a press --
  // the web fires no `click` for one and no desktop treats it as an activation
  // -- so it must not reach the responder system, a gesture handler, or
  // `onPress`. See core/PointerButtons.h.
  if (!basalt::isPressButton(button)) {
    return;
  }

  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerDown(
        hitChain(surfaceRoot_, x, y), x, y, basalt::monotonicMilliseconds());
  }

  activeTarget_ = target;
  isDown_ = true;
  emit(TouchKind::Start, target, x, y);

  // A gesture that claimed the pointer on contact -- a native handler -- takes
  // it away from React Native's responder system immediately.
  yieldToGesture(x, y);
}

void AppKitTouchDispatcher::dispatchTouchMove(double x, double y) {
  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerMove(x, y, basalt::monotonicMilliseconds());
  }

  // Motion with no button down is hover, which the touch model has no place
  // for. Reporting it would look to the responder system like a finger dragging
  // across the screen at all times.
  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  if (yieldToGesture(x, y)) {
    return;
  }
  emit(TouchKind::Move, activeTarget_, x, y);
}

void AppKitTouchDispatcher::dispatchTouchEnd(double x, double y, basalt::PointerButton button) {
  // The release half of the pointer event, for every button. Reported against
  // whatever is under the pointer now rather than what was under it on press,
  // which is what a release means when nothing was captured.
  {
    NSPoint origin = NSZeroPoint;
    const Tag over = targetUnder(surfaceRoot_, x, y, &origin);
    if (over != 0) {
      emitPointerButton(false, over, origin.x, origin.y, x, y, button);
    }
  }

  // And nothing else for a button that never pressed anything -- there is no
  // touch to end, no gesture to finish and no <Switch> to toggle.
  if (!basalt::isPressButton(button)) {
    return;
  }

  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerUp(x, y, basalt::monotonicMilliseconds());
  }

  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::End, target, x, y);

  // A <Switch> is toggled from here rather than by the toolkit control, so
  // that a press means the same thing on three desktops -- the Windows one is
  // painted and has no widget to click at all. A no-op for every view that is
  // not a switch.
  if (mountingManager_ != nullptr) {
    mountingManager_->pressedView(target);
  }
}

void AppKitTouchDispatcher::dispatchTouchCancel() {
  if (!basalt::gestures().empty()) {
    basalt::gestures().pointerCancel();
  }

  if (!isDown_ || activeTarget_ == 0) {
    return;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::Cancel, target, 0, 0);
}

// ---------------------------------------------------------------------------
// Hover
// ---------------------------------------------------------------------------

void AppKitTouchDispatcher::dispatchHover(double x, double y) {
  // Only the innermost view is reported against -- React Native's
  // PointerEventsProcessor walks up from it itself -- but the whole chain is
  // still needed, because a listener on any ancestor is reason to dispatch.
  Tag target = 0;
  NSPoint origin = NSZeroPoint;
  std::uint16_t listeners = HoverListenerNone;
  RnAppKitView *hit = RnAppKitHitTest(surfaceRoot_, x, y);
  for (NSView *view = hit; view != nil; view = view.superview) {
    if ([view isKindOfClass:[RnAppKitView class]]) {
      const auto tag = static_cast<Tag>(((RnAppKitView *)view).rnTag);
      if (target == 0) {
        target = tag;
        origin = [view convertPoint:NSZeroPoint toView:surfaceRoot_];
      }
      listeners |= mountingManager_->hoverListenersForTag(tag);
    }
    if (view == surfaceRoot_) {
      break;
    }
  }

  if (target == 0) {
    dispatchHoverLeave();
    return;
  }
  if (!hover_.admitMove(static_cast<int>(target), listeners)) {
    return;
  }
  emitPointerMove(target, origin.x, origin.y, x, y);
}

void AppKitTouchDispatcher::dispatchHoverLeave() {
  const int target = hover_.admitLeave();
  if (target == 0) {
    return;
  }
  const auto emitter = std::dynamic_pointer_cast<const TouchEventEmitter>(
      mountingManager_->eventEmitterForTag(static_cast<Tag>(target)));
  if (emitter == nullptr) {
    return;
  }
  // A leave from a platform is not forwarded: the processor reads it as the
  // pointer being gone and unwinds the path it is holding. So the coordinates
  // on it are never seen by an app, and zero is the honest answer for a
  // position that no longer exists.
  emitter->onPointerLeave(hoverEvent(0, 0, 0, 0));
}

void AppKitTouchDispatcher::emitPointerButton(bool down,
                                             Tag target,
                                             double originX,
                                             double originY,
                                             double x,
                                             double y,
                                             basalt::PointerButton button) {
  const auto emitter = std::dynamic_pointer_cast<const TouchEventEmitter>(
      mountingManager_->eventEmitterForTag(target));
  if (emitter == nullptr) {
    return;
  }
  PointerEvent event = hoverEvent(x, y, originX, originY);
  event.button = static_cast<int>(button);
  // Held *during* the event, which is a different number from `button` in the
  // same event: on release nothing is held any more. See core/PointerButtons.h.
  event.buttons = down ? static_cast<int>(basalt::buttonsMaskFor(button)) : 0;
  if (down) {
    emitter->onPointerDown(event);
  } else {
    emitter->onPointerUp(event);
  }
}

void AppKitTouchDispatcher::emitPointerMove(
    Tag target, double originX, double originY, double x, double y) {
  const auto emitter = std::dynamic_pointer_cast<const TouchEventEmitter>(
      mountingManager_->eventEmitterForTag(target));
  if (emitter == nullptr) {
    return;
  }
  emitter->onPointerMove(hoverEvent(x, y, originX, originY));
}

// A gesture recogniser that has activated owns the pointer, and React Native's
// responder system must be told the touch it was following is gone -- otherwise
// panning across a <Pressable> pans *and* presses it. This is what RNGH's
// `setJSResponder` does on the platforms it was written for; here both sides
// are fed from this one place, so it is a cancel rather than a negotiation.
bool AppKitTouchDispatcher::yieldToGesture(double x, double y) {
  if (!isDown_ || activeTarget_ == 0 || !basalt::gestures().hasActiveHandler()) {
    return false;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::Cancel, target, x, y);
  return true;
}

// Where a page point falls inside one view, for `Touch::offsetPoint`.
//
// The walk is `rnPageToLocal:fromRoot:into:`, on the view, so that it can be
// tested without a dispatcher. The page point is the fallback, which is what
// every touch carried before: the target unmounted between the press and this
// event, or a chain with no inverse. Refusing would drop the touch.
static NSPoint pointInView(basalt::AppKitMountingManager *manager,
                           RnAppKitView *root,
                           facebook::react::Tag target,
                           double pageX,
                           double pageY) {
  const NSPoint page = NSMakePoint(pageX, pageY);
  RnAppKitView *view = manager == nullptr ? nil : manager->viewForTag(target);
  if (view == nil || root == nil) {
    return page;
  }

  NSPoint local = page;
  return [view rnPageToLocal:page fromRoot:root into:&local] ? local : page;
}

void AppKitTouchDispatcher::emit(TouchKind kind, Tag target, double x, double y) {
  const auto emitter = std::dynamic_pointer_cast<const TouchEventEmitter>(
      mountingManager_->eventEmitterForTag(target));
  if (emitter == nullptr) {
    return;
  }

  const auto now = HighResTimeStamp::now();

  Touch touch{};
  touch.identifier = kPointerIdentifier;
  touch.target = target;
  // Coordinates arrive relative to the surface root, which is what React Native
  // calls the page.
  touch.pagePoint = Point{.x = static_cast<facebook::react::Float>(x),
                          .y = static_cast<facebook::react::Float>(y)};
  touch.screenPoint = touch.pagePoint;

  // Relative to the target, which is what `offsetPoint` means and what
  // `locationX`/`locationY` are built from. It carried the page point until
  // now: right only for a view at the surface's origin, and wrong by that
  // view's position for every other.
  const NSPoint offset = pointInView(mountingManager_, surfaceRoot_, target, x, y);
  touch.offsetPoint = Point{.x = static_cast<facebook::react::Float>(offset.x),
                            .y = static_cast<facebook::react::Float>(offset.y)};
  touch.force = 1.0F;
  touch.timeStamp = now;

  Touches changed{};
  changed.insert(touch);

  const bool isEnd = kind == TouchKind::End || kind == TouchKind::Cancel;

  TouchEvent event{};
  event.changedTouches = changed;
  // On touchend the finger is gone, so it is no longer in `touches`. Getting
  // this wrong leaves the responder system believing a touch is still active,
  // which swallows the next press.
  event.touches = isEnd ? Touches{} : changed;
  event.targetTouches = event.touches;

  switch (kind) {
    case TouchKind::Start:
      emitter->onTouchStart(event);
      break;
    case TouchKind::Move:
      emitter->onTouchMove(event);
      break;
    case TouchKind::End:
      emitter->onTouchEnd(event);
      break;
    case TouchKind::Cancel:
      emitter->onTouchCancel(event);
      break;
  }
}

} // namespace basalt
