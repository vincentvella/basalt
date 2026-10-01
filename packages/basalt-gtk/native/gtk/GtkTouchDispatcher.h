// GTK input into React Native's touch events.
//
// React Native's Pressability -- what backs <Pressable>, <TouchableOpacity> and
// every onPress in an app -- runs on the responder system in JavaScript, and
// the responder system is fed by touchstart/touchmove/touchend. So a pointer on
// a desktop is reported here as a single touch point, which is also what React
// Native for Windows and macOS do. W3C pointer events exist alongside these and
// are only consulted for hover, behind a feature flag.
//
// Controllers are attached to the surface root, not to every view. A GTK
// controller per widget would mean creating and destroying controllers on every
// mutation, and would still need the same hit test: `gtk_widget_pick` already
// walks the widget tree and returns the deepest widget at a point, which is the
// same answer React Native's own hit testing is looking for now that every view
// is allocated at the frame Yoga gave it.

#pragma once

#include "GtkMountingManager.h"
#include "HoverTracker.h"
#include "PointerButtons.h"
#include "RnView.h"

#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/graphics/Point.h>

namespace basalt {

// The tag of the React Native view under a point, in `root`'s coordinates, or 0
// if the point hits nothing.
//
// A free function because it is a pure function of the widget tree and nothing
// else, which is also what makes it testable: hit testing is the part of input
// most likely to be quietly wrong, and it needs no gestures to exercise.
facebook::react::Tag hitTestTag(RnView *root, double x, double y);

class GtkTouchDispatcher {
 public:
  GtkTouchDispatcher(GtkMountingManager *mountingManager, RnView *surfaceRoot);
  ~GtkTouchDispatcher();

  GtkTouchDispatcher(const GtkTouchDispatcher &) = delete;
  GtkTouchDispatcher &operator=(const GtkTouchDispatcher &) = delete;
  GtkTouchDispatcher(GtkTouchDispatcher &&) = delete;
  GtkTouchDispatcher &operator=(GtkTouchDispatcher &&) = delete;

  // Synthesises a press and release at a point in surface-root coordinates,
  // entering at the same place the GTK gesture callbacks do.
  //
  // This exists because the input path is otherwise untestable in automation:
  // synthesising a real pointer event means driving the window system, which on
  // macOS needs accessibility permission a headless run does not have. It skips
  // GDK's event delivery and nothing else, so it proves hit testing, emitter
  // lookup and event-beat delivery, but not that GTK routes clicks here.
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
  // reason as the two above, and the same entry point the motion controller
  // uses. A negative coordinate means the pointer left the surface.
  void synthesiseHover(double x, double y);

 private:
  static void onPressed(GtkGestureClick *gesture, int count, double x, double y, gpointer userData);
  static void onReleased(GtkGestureClick *gesture, int count, double x, double y, gpointer userData);
  static void onCancelled(GtkGesture *gesture, GdkEventSequence *sequence, gpointer userData);
  static void onMotion(GtkEventControllerMotion *controller, double x, double y, gpointer userData);
  static void onPointerLeft(GtkEventControllerMotion *controller, gpointer userData);

  // `button` decides whether this presses anything. Only the primary one drives
  // the touch model; a secondary or middle click produces a pointer event and
  // nothing else, which is what keeps a right-click from firing every
  // `<Pressable>` it lands on. See core/PointerButtons.h.
  void dispatchTouchStart(double x, double y, basalt::PointerButton button);
  void dispatchTouchMove(double x, double y);
  void dispatchTouchEnd(double x, double y, basalt::PointerButton button);
  void dispatchTouchCancel();

  // The hover half. Separate from dispatchTouchMove because the two answer
  // different questions about the same motion event: the touch model asks which
  // view the finger went down on, and hover asks which views the cursor is
  // inside right now.
  void dispatchHover(double x, double y);
  void dispatchHoverLeave();

  enum class TouchKind { Start, Move, End, Cancel };

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

  GtkMountingManager *mountingManager_;
  RnView *surfaceRoot_;

  GtkGesture *clickGesture_{nullptr};
  GtkEventController *motionController_{nullptr};

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
