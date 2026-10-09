// AppKit input into React Native's touch events.
//
// React Native's Pressability -- what backs <Pressable>, <TouchableOpacity> and
// every onPress in an app -- runs on the responder system in JavaScript, and the
// responder system is fed by touchstart/touchmove/touchend. So a pointer on a
// desktop is reported here as a single touch point, which is also what React
// Native for Windows and macOS do. W3C pointer events exist alongside these and
// are only consulted for hover, behind a feature flag.
//
// Deliberately the same shape as basalt-gtk's GtkTouchDispatcher,
// down to the state machine and the event construction, because the part that
// is easy to get subtly different -- which touches are in `touches` versus
// `changedTouches`, and which view a gesture is reported against -- is not
// about the toolkit at all. If the two ever diverge, an app's onPress will fire
// on one desktop and not the other.

#pragma once

#import "RnAppKitView.h"

#include "AppKitMountingManager.h"
#include "HoverTracker.h"
#include "PointerButtons.h"

#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/graphics/Point.h>

namespace basalt {

// The tag of the React Native view under a point, in the root's coordinates, or
// 0 if the point hits nothing. The view-tree half of this is
// `RnAppKitHitTest`, which lives in the view layer and needs no React Native.
facebook::react::Tag hitTestTag(RnAppKitView *root, double x, double y);

class AppKitTouchDispatcher {
 public:
  AppKitTouchDispatcher(AppKitMountingManager *mountingManager, RnAppKitView *surfaceRoot);
  ~AppKitTouchDispatcher();

  AppKitTouchDispatcher(const AppKitTouchDispatcher &) = delete;
  AppKitTouchDispatcher &operator=(const AppKitTouchDispatcher &) = delete;
  AppKitTouchDispatcher(AppKitTouchDispatcher &&) = delete;
  AppKitTouchDispatcher &operator=(AppKitTouchDispatcher &&) = delete;

  // Synthesises a press and release at a point in surface-root coordinates,
  // entering at the same place the AppKit callbacks do.
  //
  // This exists because the input path is otherwise untestable in automation:
  // synthesising a real mouse event on macOS means CGEvent, which needs
  // accessibility permission an automated run does not have. It skips AppKit's
  // event delivery and nothing else, so it proves hit testing, emitter lookup
  // and event-beat delivery, but not that AppKit routes clicks here.
  void synthesiseTap(double x, double y);
  // The same, with a button. BASALT_TEST_SECONDARY_TAP is what needs it: a
  // right-click cannot be injected any other way, and it is the one click whose
  // whole point is that it does *not* press what it lands on.
  void synthesiseTap(double x, double y, basalt::PointerButton button);

  // A press, a run of moves, and a release. The same reason as synthesiseTap,
  // one step further: a gesture recogniser cannot be exercised by a tap at all
  // -- a pan is defined by the movement between the two -- so a drag has to be
  // injectable for anything about it to be provable outside a person's hand.
  void synthesiseDrag(double fromX, double fromY, double toX, double toY, int steps);

  // Moves the pointer without pressing it, which is what produces hover. Same
  // reason as the two above, and the same entry point the tracking area uses. A
  // negative coordinate means the pointer left the surface.
  void synthesiseHover(double x, double y);

  // The main-thread half, called by the view that received the mouse event.
  // Public because the Objective-C trampoline has to reach them.
  // `button` decides whether this presses anything. Only the primary one drives
  // the touch model; a secondary or middle click produces a pointer event and
  // nothing else. See core/PointerButtons.h.
  void dispatchTouchStart(double x, double y, basalt::PointerButton button);
  void dispatchTouchMove(double x, double y);
  void dispatchTouchEnd(double x, double y, basalt::PointerButton button);
  void dispatchTouchCancel();

  // Starts a drag out of the window if the press began on a view marked as a
  // drag source. True when it did, and the caller should then cancel the
  // touch: AppKit owns the gesture from that point and the touch will never
  // end. See core/DragAndDrop.h.
  bool beginDragOut(double x, double y);

  // The hover half. Separate from dispatchTouchMove because the two answer
  // different questions about the same motion: the touch model asks which view
  // the finger went down on, and hover asks which views the cursor is inside
  // right now.
  void dispatchHover(double x, double y);
  void dispatchHoverLeave();

 private:
  enum class TouchKind { Start, Move, End, Cancel };

  // The window stopped being key while the mouse was down.
  //
  // React Native expects a touch to end one of two ways, and a press that
  // simply stops is the shape that leaves a `<Pressable>` highlighted for the
  // rest of the screen's life: `onPressOut` never runs and the responder is
  // never released. AppKit has no gesture recogniser to cancel, which is what
  // the GTK host uses (`GtkGesture::cancelled`); this is the nearest thing, and
  // it is what happens when the person switches app or a panel takes over
  // mid-press.
  //
  // Observed here rather than in the host's window delegate so that the whole
  // path has a test: the delegate is in `main_appkit.mm`, which the suite does
  // not link, and the part worth testing is that the notification reaches the
  // press.
  void windowResignedKey(void *window);

  // Hands the pointer to a gesture recogniser that has activated, cancelling
  // React Native's touch. True when that happened, which means the caller has
  // nothing left to report.
  bool yieldToGesture(double x, double y);

  // Builds the TouchEvent for the current gesture. React Native wants three
  // lists -- all touches, the ones that changed, and the ones that started on
  // the event target -- which with one pointer are the same list, except on
  // touchend where nothing is touching any more.
  void emit(TouchKind kind, facebook::react::Tag target, double x, double y);

  // A pointerMove at the view under the cursor. The only pointer event a host
  // emits for hover; see core/HoverTracker.h.
  // A press or a release as a pointer event, which is the only form a
  // non-primary click arrives in.
  void emitPointerButton(bool down,
                         facebook::react::Tag target,
                         double originX,
                         double originY,
                         double x,
                         double y,
                         basalt::PointerButton button);
  void emitPointerMove(
      facebook::react::Tag target, double originX, double originY, double x, double y);

  AppKitMountingManager *mountingManager_;
  RnAppKitView *surfaceRoot_;
  // The Objective-C object the root forwards mouse events to. Held so it
  // outlives the root's weak reference to it.
  id inputTarget_;
  // The notification observer for a window that stops being key; see
  // windowResignedKey.
  id resignObserver_;

  // The view a gesture started on. React Native reports every touch in a
  // gesture against the target it began on, even after the pointer leaves that
  // view, because that is what the responder system expects.
  facebook::react::Tag activeTarget_{0};
  bool isDown_{false};

  // Where the cursor was, so that enter and leave can be told from over and
  // out. See core/HoverTracker.h.
  HoverTracker hover_;
};

} // namespace basalt
