// <ScrollView> on GTK4.
//
// Yoga does the hard part. A ScrollView's Yoga node carries `overflow: scroll`,
// which lets its child exceed the viewport instead of being clamped to it, so
// by the time a mutation arrives the content is already laid out at its full
// size and the only thing missing is the offset.
//
// Structure: a mounted ScrollView has exactly **one** child, a content view.
// React Native's JS wraps the children in an `RCTScrollContentView`, which the
// C++ registry rewrites to a plain `View` (`componentNameByReactViewName`), so
// no extra descriptor is needed and nothing here should expect N children.
//
// Scrolling is expressed as an offset on the ScrollView's own RnView rather
// than by a GtkScrolledWindow. GtkScrolledWindow wants to size its child
// through the measure/allocate protocol, and this platform's whole invariant is
// that React Native decides sizes and the layout manager only places things.
// Shifting children in `RnLayout::allocate` keeps that invariant and makes GTK's
// hit testing follow the scroll for free.
//
// Two things must happen on every scroll, and they are not the same thing:
//
//   - `onScroll` goes to JavaScript, throttled by `scrollEventThrottle`. Without
//     it VirtualizedList never renders past its first window.
//   - `contentOffset` is written back into `ScrollViewState`, unthrottled.
//     `ScrollViewShadowNode::getContentOriginOffset` reads it, and through that
//     so do `measure`, `measureLayout`, C++ hit testing and view culling. Skip
//     it and those all silently report unscrolled coordinates.

#pragma once

#include "RnView.h"
#include "ScrollAnimation.h"
#include "ScrollBounds.h"
#include "ScrollIndicator.h"
#include "ScrollSnap.h"
#include "ScrollMomentum.h"

#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <functional>
#include <unordered_map>

namespace basalt {

class GtkScrollViewManager {
 public:
  // Emitters live in the mounting manager's registry, which is also what owns
  // this, so they are reached through a lookup rather than a second copy.
  using EmitterLookup = std::function<facebook::react::EventEmitter::Shared(facebook::react::Tag)>;

  explicit GtkScrollViewManager(EmitterLookup lookup);

  // Called for every mutation touching a ScrollView. Attaches the scroll
  // controller the first time, and refreshes the geometry and props after.
  void update(RnView *view, const facebook::react::ShadowView &shadowView);

  void remove(facebook::react::Tag tag);

  // Starts a fling at a velocity in pixels per second, as GTK's `decelerate`
  // reports one. Returns false if it was too slow to be worth animating.
  //
  // Public, and not only because the signal handler is static. The coasting is
  // otherwise untestable: a real fling needs a touchscreen, and the frame clock
  // that drives it needs a mapped window and two seconds of main loop. This and
  // `advanceFling` are the same escape hatch `GtkTouchDispatcher::synthesiseTap`
  // is, entering exactly where GDK does and skipping nothing above it.
  bool fling(facebook::react::Tag tag, double velocityX, double velocityY);

  // Advances a running fling by `seconds`, applying the offset and emitting
  // what falls out of it. Returns false once it has finished, which is also
  // when onMomentumScrollEnd has been emitted.
  bool advanceFling(facebook::react::Tag tag, double seconds);

  // Advances an animated `scrollTo` -- or the settle a snapping list does at
  // the end of a fling -- by `seconds`, returning false once it has arrived.
  //
  // Public for the same reason `advanceFling` is: the animation runs on a frame
  // clock, and a frame clock needs a mapped window and a main loop. Without
  // this a paging list could be shown to *decide* on a page and never shown to
  // reach one.
  bool advanceAnimation(facebook::react::Tag tag, double seconds);

  // ScrollView's imperative commands: scrollTo, scrollToEnd. Returns false if
  // the command is not one this handles.
  bool dispatchCommand(facebook::react::Tag tag, const std::string &name, const folly::dynamic &args);

  // A wheel notch carries no pixel distance of its own, so a step has to be
  // chosen. Roughly three lines of 16pt text, which is what browsers and both
  // other desktops settle on. Public because the test instrument resolves
  // notches into pixels before calling `scrollAt`, and the two must use one
  // number.
  static constexpr double kWheelStepPixels = 53.0;

  // A wheel over a point, in the surface root's coordinates, for
  // BASALT_TEST_SCROLL. Returns false when nothing under it scrolls.
  //
  // It exists for the same reason every other instrument does: a real wheel
  // event needs a display server that will deliver one to a window it
  // considers focused, and an automated run on a headless compositor has no
  // such thing. This enters where the GtkEventControllerScroll callback does,
  // so it exercises the hit test, the offset, the clamp, the state write-back,
  // onScroll and the pull gesture, and skips only GDK's delivery.
  bool scrollAt(RnView *root, double x, double y, double dx, double dy);

  // The pull past the top of the list, which is what a <RefreshControl> is
  // waiting for. Called with how far past the top this scroll asked to go, or
  // with zero when the gesture went the other way or the view actually moved --
  // which re-arms it. See core/PullToRefresh.h for why a desktop has to count
  // this rather than measure a rubber band.
  void setOverscrollTopHandler(std::function<void(facebook::react::Tag, double)> handler) {
    overscrollTop_ = std::move(handler);
  }



 private:
  std::function<void(facebook::react::Tag, double)> overscrollTop_;

  struct Entry {
    RnView *view{nullptr};
    facebook::react::Tag tag{0};
    GtkScrollViewManager *owner{nullptr};

    // The GTK callbacks below hold a pointer to this Entry. std::unordered_map
    // never moves its elements, so that pointer stays valid for the life of the
    // entry -- but erasing one would leave the controller pointing at freed
    // memory, so remove() detaches the controller first.
    GtkEventController *controller{nullptr};

    std::shared_ptr<const facebook::react::ScrollViewShadowNode::ConcreteState> state;

    facebook::react::Size contentSize{};
    facebook::react::Size containerSize{};
    // `contentInset` changes how far the view scrolls;
    // `scrollIndicatorInsets` changes only where the bar is drawn. See
    // core/ScrollBounds.h.
    facebook::react::EdgeInsets contentInset{};
    facebook::react::EdgeInsets indicatorInset{};

    bool scrollEnabled{true};
    // `showsVerticalScrollIndicator` / `showsHorizontalScrollIndicator`. Both
    // default to true, as React Native's own props do.
    bool showsVerticalIndicator{true};
    bool showsHorizontalIndicator{true};
    // Milliseconds, as React Native's prop is. Zero means every scroll.
    double eventThrottleMs{0};

    double offsetX{0};
    double offsetY{0};
    gint64 lastEmitMicros{0};
    bool dragging{false};

    // The fling after the fingers left. GTK hands over a velocity and stops, so
    // the coasting is run here: a tick callback on the view, advancing the
    // model, until it falls below a pixel a frame or hits an edge.
    ScrollMomentum momentum;
    guint momentumTickId{0};
    // An animated `scrollTo`, which shares the frame clock with the fling above
    // and never runs at the same time as one: each stops the other.
    ScrollAnimation animation;
    guint animationTickId{0};
    gint64 animationLastMicros{0};
    gint64 momentumLastMicros{0};
    // React Native's decelerationRate prop, read off the ScrollView's props so
    // an app that asks for a fast fling gets one.
    double decelerationRate{ScrollMomentum::kNormalDeceleration};
    // `pagingEnabled`, `snapToInterval`, `snapToOffsets`, `snapToAlignment`.
    ScrollSnapConfig snap;
  };

  static gboolean onScroll(GtkEventControllerScroll *controller, double dx, double dy, gpointer userData);
  // The part of onScroll that is not about GTK's event controller, so the test
  // instrument and a real wheel go through the same code.
  void scrollEntry(Entry &entry, double dx, double dy);
  static void onScrollBegin(GtkEventControllerScroll *controller, gpointer userData);
  static void onScrollEnd(GtkEventControllerScroll *controller, gpointer userData);
  static void onDecelerate(GtkEventControllerScroll *controller,
                           double velocityX,
                           double velocityY,
                           gpointer userData);
  static gboolean onMomentumTick(GtkWidget *widget, GdkFrameClock *clock, gpointer userData);
  static gboolean onAnimationTick(GtkWidget *widget, GdkFrameClock *clock, gpointer userData);
  void scrollTowards(Entry &entry, double x, double y, bool animated);
  // Settles a drag or a fling on a snap point, if the props ask for one.
  // Returns whether it took over.
  bool settleOnSnapPoint(Entry &entry, double velocityY);

  void stopAnimation(Entry &entry);

  // Ends a fling, with or without telling JavaScript. `emitEnd` is false only
  // when the ScrollView itself is going away, where the emitter is about to
  // stop existing anyway.
  void stopMomentum(Entry &entry, bool emitEnd);

  void applyOffset(Entry &entry, double x, double y, bool emitEvent);
  // Recomputes both overlay scrollbars and hands them to the view to draw.
  // Called wherever the offset, the content size or the container size can
  // have changed, which is more places than the offset alone.
  void updateIndicators(const Entry &entry);
  void emitScrollEvent(Entry &entry, const char *which);
  void writeStateOffset(const Entry &entry);

  EmitterLookup lookup_;
  std::unordered_map<facebook::react::Tag, Entry> entries_;
};

} // namespace basalt
