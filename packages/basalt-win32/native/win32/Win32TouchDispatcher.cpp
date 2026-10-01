#include "Win32TouchDispatcher.h"

#include "Gestures.h"

#include <react/renderer/components/view/PointerEvent.h>
#include <react/renderer/components/view/TouchEvent.h>

#include <cstdint>

namespace basalt {

using facebook::react::HighResTimeStamp;
using facebook::react::Point;
using facebook::react::PointerEvent;
using facebook::react::Tag;
using facebook::react::Touch;
using facebook::react::TouchEvent;
using facebook::react::TouchEventEmitter;
using facebook::react::Touches;
using win32::RnWin32View;

namespace {

// A desktop pointer is one touch point, and React Native identifies touches by
// number. Zero is the first (and here only) finger. The same number identifies
// the pointer in pointer events, where React Native keys its hover tracking by
// it.
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

// A point through a 2D affine matrix in Direct2D's Matrix3x2F order and its
// row-vector convention, which is the order `localToParent` writes.
void mapPoint(const float m[6], double x, double y, double &outX, double &outY) {
  const double mappedX = m[0] * x + m[2] * y + m[4];
  const double mappedY = m[1] * x + m[3] * y + m[5];
  outX = mappedX;
  outY = mappedY;
}

// The views under a point, innermost first, each with its origin in the
// surface root's coordinates. That is what a gesture recogniser needs and a
// touch does not: a touch is reported against one target, while a gesture may
// be attached to any ancestor of the view that was hit.
//
// The origin is carried *up* the chain rather than recomputed per view, so the
// walk stays linear and each view's contribution -- its own transform, then its
// parent's scroll offset -- is applied exactly once. AppKit gets the same
// answer from convertPoint:toView: and GTK from the widget's allocation; here
// there is no toolkit geometry, so the composition is written out. It is the
// mirror of what `hitTest` did on the way down, which is the only thing that
// makes the two agree about a rotated or scrolled ancestor.
//
// Built only when something is attached; see core/Gestures.h.
std::vector<basalt::HitView> hitChain(RnWin32View *root, double x, double y) {
  std::vector<basalt::HitView> chain;
  RnWin32View *hit = win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));
  if (hit == nullptr) {
    return chain;
  }

  double originX = 0.0;
  double originY = 0.0;
  for (RnWin32View *view = hit; view != nullptr; view = view->parent()) {
    chain.push_back(basalt::HitView{
        .tag = static_cast<int>(view->tag()), .originX = originX, .originY = originY});
    if (view == root) {
      break;
    }
    RnWin32View *parent = view->parent();
    if (parent == nullptr) {
      break;
    }
    float local[6];
    view->localToParent(local);
    mapPoint(local, originX, originY, originX, originY);
    originX -= parent->scrollX();
    originY -= parent->scrollY();
  }
  return chain;
}

} // namespace

Win32TouchDispatcher::Win32TouchDispatcher(
    Win32MountingManager *mountingManager,
    RnWin32View *surfaceRoot)
    : mountingManager_(mountingManager), surfaceRoot_(surfaceRoot) {}

void Win32TouchDispatcher::synthesiseTap(double x, double y) {
  synthesiseTap(x, y, basalt::PointerButton::Primary);
}

void Win32TouchDispatcher::synthesiseTap(double x, double y, basalt::PointerButton button) {
  dispatchTouchStart(x, y, button);
  dispatchTouchEnd(x, y, button);
}

void Win32TouchDispatcher::synthesiseHover(double x, double y) {
  if (x < 0 || y < 0) {
    dispatchHoverLeave();
    return;
  }
  dispatchHover(x, y);
}

void Win32TouchDispatcher::synthesiseDrag(
    double fromX, double fromY, double toX, double toY, int steps) {
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

Tag hitTestTag(RnWin32View *root, double x, double y) {
  RnWin32View *hit = win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));
  return hit == nullptr ? 0 : static_cast<Tag>(hit->tag());
}

// ---------------------------------------------------------------------------
// Dispatch
// ---------------------------------------------------------------------------

void Win32TouchDispatcher::dispatchTouchStart(double x, double y, basalt::PointerButton button) {
  if (surfaceRoot_ == nullptr) {
    return;
  }

  // The innermost view, and where inside it the press landed. Needed for the
  // pointer event whichever button it was; PointerEventsProcessor walks up from
  // there itself.
  Tag target = 0;
  double originX = 0;
  double originY = 0;
  for (const auto &view : hitChain(surfaceRoot_, x, y)) {
    target = static_cast<Tag>(view.tag);
    originX = view.originX;
    originY = view.originY;
    break;
  }
  if (target == 0) {
    return;
  }

  // Every button produces a pointer event, which is where `button` can be said
  // at all.
  emitPointerButton(true, target, originX, originY, x, y, button);

  // Only the primary one goes any further. A secondary click is not a press --
  // the web fires no `click` for one and no desktop treats it as an activation
  // -- so it must not reach the responder system, a gesture handler, or
  // `onPress`. See core/PointerButtons.h.
  //
  // This host was the odd one out in the other direction: it handled only
  // WM_LBUTTONDOWN, so a right-click did nothing whatsoever.
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

void Win32TouchDispatcher::dispatchTouchMove(double x, double y) {
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

void Win32TouchDispatcher::dispatchTouchEnd(double x, double y, basalt::PointerButton button) {
  // The release half of the pointer event, for every button. Reported against
  // whatever is under the pointer now rather than what was under it on press,
  // which is what a release means when nothing was captured.
  if (surfaceRoot_ != nullptr) {
    for (const auto &view : hitChain(surfaceRoot_, x, y)) {
      emitPointerButton(
          false, static_cast<Tag>(view.tag), view.originX, view.originY, x, y, button);
      break;
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

  // A <Switch> on this host is painted rather than mounted, so nothing
  // underneath it turns a click into a toggle -- GTK's GtkSwitch and AppKit's
  // NSSwitch both do that for themselves. This is the one line that makes up
  // the difference; it is a no-op for every view that is not a switch.
  if (mountingManager_ != nullptr) {
    mountingManager_->pressedView(target);
  }
}

void Win32TouchDispatcher::dispatchTouchCancel() {
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

void Win32TouchDispatcher::dispatchHover(double x, double y) {
  // The same chain the gesture path uses -- this is the one place where the
  // absence of a toolkit pays off, because `hitChain` already composes the
  // transforms and scroll offsets that GTK and AppKit answer for themselves.
  //
  // Only the innermost view is reported against -- React Native's
  // PointerEventsProcessor walks up from it itself -- but the whole chain is
  // still needed, because a listener on any ancestor is reason to dispatch.
  Tag target = 0;
  double originX = 0;
  double originY = 0;
  std::uint16_t listeners = HoverListenerNone;
  for (const auto &view : hitChain(surfaceRoot_, x, y)) {
    if (target == 0) {
      target = static_cast<Tag>(view.tag);
      originX = view.originX;
      originY = view.originY;
    }
    listeners |= mountingManager_->hoverListenersForTag(static_cast<Tag>(view.tag));
  }

  if (target == 0) {
    dispatchHoverLeave();
    return;
  }
  if (!hover_.admitMove(static_cast<int>(target), listeners)) {
    return;
  }
  emitPointerMove(target, originX, originY, x, y);
}

void Win32TouchDispatcher::dispatchHoverLeave() {
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
  // on it are never seen by an app, which is just as well -- WM_MOUSELEAVE
  // reports no position at all.
  emitter->onPointerLeave(hoverEvent(0, 0, 0, 0));
}

void Win32TouchDispatcher::emitPointerButton(bool down,
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

void Win32TouchDispatcher::emitPointerMove(
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
// setJSResponder does on the platforms it was written for; here both sides are
// fed from this one place, so it is a cancel rather than a negotiation.
bool Win32TouchDispatcher::yieldToGesture(double x, double y) {
  if (!isDown_ || activeTarget_ == 0 || !basalt::gestures().hasActiveHandler()) {
    return false;
  }
  const Tag target = activeTarget_;
  isDown_ = false;
  activeTarget_ = 0;
  emit(TouchKind::Cancel, target, x, y);
  return true;
}

void Win32TouchDispatcher::emit(TouchKind kind, Tag target, double x, double y) {
  if (mountingManager_ == nullptr) {
    return;
  }
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
  //
  // The page point is the fallback, which is what every touch carried before:
  // the target unmounted between the press and this event, or a chain with no
  // inverse. Refusing would drop the touch.
  float offsetX = static_cast<float>(x);
  float offsetY = static_cast<float>(y);
  if (mountingManager_ != nullptr) {
    if (RnWin32View *view = mountingManager_->viewForTag(target)) {
      view->pageToLocal(surfaceRoot_, static_cast<float>(x), static_cast<float>(y),
                        offsetX, offsetY);
    }
  }
  touch.offsetPoint = Point{.x = static_cast<facebook::react::Float>(offsetX),
                            .y = static_cast<facebook::react::Float>(offsetY)};
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
