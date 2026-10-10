// Win32 mouse messages into React Native's touch events.
//
// React Native's Pressability -- what backs <Pressable>, <TouchableOpacity> and
// every onPress in an app -- runs on the responder system in JavaScript, and the
// responder system is fed by touchstart/touchmove/touchend. So a pointer on a
// desktop is reported here as a single touch point, which is also what React
// Native for Windows and macOS do. W3C pointer events exist alongside these and
// are only consulted for hover, behind a feature flag.
//
// Deliberately the same shape as the GTK and AppKit dispatchers, down to the
// state machine and the event construction, because the part that is easy to
// get subtly different -- which touches are in `touches` versus
// `changedTouches`, and which view a gesture is reported against -- is not
// about the toolkit at all. If the three ever diverge, an app's onPress will
// fire on two desktops and not the third.
//
// What is different here is what the toolkit does *not* provide. GTK has
// `gtk_widget_pick` and AppKit has `-hitTest:`, so on those two the dispatcher
// asks the toolkit which widget was pressed. A Win32 view is a plain C++
// object with no window of its own, so the whole of hit testing is this
// project's: `hitTest` in RnWin32View.h, which this calls. The same is true of
// capture -- AppKit and GTK route the drag to the widget that took the press
// for free, and here the host has to call SetCapture.

#pragma once

#include "HoverTracker.h"
#include "PointerButtons.h"
#include "RnWin32View.h"
#include "TextSelection.h"
#include "Win32MountingManager.h"

#include <react/renderer/components/view/TouchEventEmitter.h>
#include <react/renderer/graphics/Point.h>

#include <functional>
#include <utility>

namespace basalt {

// The tag of the React Native view under a point, in the surface root's
// coordinates, or 0 if the point hits nothing. The view-tree half of this is
// `win32::hitTest`, which lives in the view layer and needs no React Native.
facebook::react::Tag hitTestTag(win32::RnWin32View *root, double x, double y);

class Win32TouchDispatcher {
 public:
  Win32TouchDispatcher(Win32MountingManager *mountingManager,
                       win32::RnWin32View *surfaceRoot);
  ~Win32TouchDispatcher();

  Win32TouchDispatcher(const Win32TouchDispatcher &) = delete;
  Win32TouchDispatcher &operator=(const Win32TouchDispatcher &) = delete;
  Win32TouchDispatcher(Win32TouchDispatcher &&) = delete;
  Win32TouchDispatcher &operator=(Win32TouchDispatcher &&) = delete;

  // The surface root can be replaced after a surface restart; the dispatcher
  // outlives it because the host owns both and tears them down together.
  //
  // The hover state goes with it. Every tag in the old tree is about to stop
  // existing, and a leave dispatched at one of them would be a lookup of an
  // emitter that is gone.
  void setSurfaceRoot(win32::RnWin32View *surfaceRoot) {
    surfaceRoot_ = surfaceRoot;
    hover_.forget();
  }

  // Synthesises a press and release at a point in surface-root coordinates,
  // entering at the same place the window procedure does.
  //
  // This exists because the input path is otherwise untestable in automation:
  // synthesising a real mouse event on Windows means SendInput, which moves the
  // actual cursor and so cannot run beside anything else on the machine. It
  // skips Win32's event delivery and nothing else, so it proves hit testing,
  // emitter lookup and event-beat delivery, but not that Windows routes clicks
  // here.
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

  // --- Selecting text -------------------------------------------------------
  //
  // The Win32 half of what the other two dispatchers do, in the same three
  // calls and for the same reasons: a press inside a selectable paragraph is
  // remembered and nothing else, and the gesture becomes a selection once the
  // pointer has moved far enough to be a sweep rather than a tap -- at which
  // point the touch sequence this was reporting is cancelled. The state machine
  // is core/TextSelection.h.

  // How this host asks for a repaint, which it does per transaction rather than
  // per view: a selection changed by the pointer is not a transaction, so the
  // thing that changed it has to ask. Left unset in the tests, which render on
  // demand. See requestRepaint in main_win32.cpp.
  void setRepaintRequester(std::function<void()> requestRepaint) {
    requestRepaint_ = std::move(requestRepaint);
  }

  // Moves the pointer without pressing it, which is what produces hover. Same
  // reason as the two above, and the same entry point WM_MOUSEMOVE uses. A
  // negative coordinate means the pointer left the window.
  void synthesiseHover(double x, double y);

  // The UI-thread half, called by the host's window procedure. Coordinates are
  // client-area pixels, which are the surface root's own coordinates: the host
  // sizes the root to the client rectangle, so the two spaces are the same one.
  // `button` decides whether this presses anything. Only the primary one drives
  // the touch model; a secondary or middle click produces a pointer event and
  // nothing else. See core/PointerButtons.h.
  void dispatchTouchStart(double x, double y, basalt::PointerButton button);
  void dispatchTouchMove(double x, double y);
  void dispatchTouchEnd(double x, double y, basalt::PointerButton button);
  void dispatchTouchCancel();

  // The hover half. Separate from dispatchTouchMove because the two answer
  // different questions about the same WM_MOUSEMOVE: the touch model asks which
  // view the finger went down on, and hover asks which views the cursor is
  // inside right now.
  void dispatchHover(double x, double y);
  void dispatchHoverLeave();

  // Whether a press is outstanding. The host reads this to decide whether a
  // WM_MOUSEMOVE is a drag worth reporting and whether to release capture.
  bool isDown() const { return isDown_; }

 private:
  // The innermost selectable paragraph under a point in root coordinates, and
  // the UTF-16 offset in its text, or {nullptr, -1}.
  std::pair<win32::RnWin32View *, int> selectableTextAt(double x, double y) const;

  // The offset in `view`'s text nearest a point in *root* coordinates, which is
  // not the same question: a drag that has left the paragraph still extends the
  // selection inside it.
  int textIndexIn(win32::RnWin32View *view, double x, double y) const;

  // Pushes the model's range onto the paragraph it belongs to, takes the
  // highlight off whatever held one before, and asks for a repaint.
  void drawSelection();

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

  Win32MountingManager *mountingManager_;

  // What is selected, which paragraph it is in, and which is holding a
  // highlight -- the last two differ when a selection moves to another
  // paragraph, and after a release the highlight outlives the drag.
  //
  // Raw pointers into the view tree, cleared on press and in the destructor.
  // This host's views are owned by the mounting manager and outlive a
  // transaction, and a paragraph unmounted mid-selection is the case
  // `clearSelection` exists for.
  basalt::TextSelection selection_;
  win32::RnWin32View *selectionView_ = nullptr;
  win32::RnWin32View *highlightView_ = nullptr;
  std::function<void()> requestRepaint_;
  win32::RnWin32View *surfaceRoot_;

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
