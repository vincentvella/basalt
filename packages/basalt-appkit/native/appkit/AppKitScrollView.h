// <ScrollView> on AppKit.
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
// Scrolling is `bounds.origin` on the ScrollView's own view, not an
// `NSScrollView`. An NSScrollView wants to own its document view's size and
// bring a clip view, scrollers and elasticity with it; this platform's whole
// invariant is that React Native decides sizes and the platform only places.
// Moving the bounds keeps that invariant and makes AppKit's drawing, clipping
// and hit testing follow the scroll for free. The GTK side reaches the same
// place by shifting children in its layout manager.
//
// Two things must happen on every scroll, and they are not the same thing:
//
//   - `onScroll` goes to JavaScript, throttled by `scrollEventThrottle`. Without
//     it VirtualizedList never renders past its first window.
//   - `contentOffset` is written back into `ScrollViewState`, unthrottled.
//     `ScrollViewShadowNode::getContentOriginOffset` reads it, and through that
//     so do `measure`, `measureLayout`, C++ hit testing and view culling. Skip
//     it and those all silently report unscrolled coordinates.
//
// All of that is the same on both desktops, which is why this file reads like
// GtkScrollView.cpp with different event plumbing.

#pragma once

#import "RnAppKitView.h"

#include "ScrollAnimation.h"
#include "ScrollBounds.h"
#include "ScrollIndicator.h"
#include "ScrollSnap.h"

#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <functional>
#include <unordered_map>

namespace basalt {

class AppKitScrollViewManager {
 public:
  // Emitters live in the mounting manager's registry, which is also what owns
  // this, so they are reached through a lookup rather than a second copy.
  using EmitterLookup = std::function<facebook::react::EventEmitter::Shared(facebook::react::Tag)>;

  explicit AppKitScrollViewManager(EmitterLookup lookup);
  ~AppKitScrollViewManager();

  AppKitScrollViewManager(const AppKitScrollViewManager &) = delete;
  AppKitScrollViewManager &operator=(const AppKitScrollViewManager &) = delete;

  // Called for every mutation touching a ScrollView. Registers the view the
  // first time, and refreshes the geometry and props after.
  void update(RnAppKitView *view, const facebook::react::ShadowView &shadowView);

  void remove(facebook::react::Tag tag);

  // ScrollView's imperative commands: scrollTo, scrollToEnd. Returns false if
  // the command is not one this handles.
  bool dispatchCommand(facebook::react::Tag tag,
                       const std::string &name,
                       const folly::dynamic &args);

  // The wheel, from the Objective-C trampoline. Returns false when the scroll
  // was not consumed, which lets AppKit walk it up the responder chain.
  //
  // `began`/`ended` are the fingers on a touchpad, and become onScrollBeginDrag
  // and onScrollEndDrag. `momentumBegan`/`momentumEnded` are the coasting that
  // follows them, and become onMomentumScrollBegin and onMomentumScrollEnd. A
  // wheel has neither: it reports no phase at all, so it is a run of plain
  // scrolls, which is what it is.
  bool scrollBy(facebook::react::Tag tag,
                double dx,
                double dy,
                bool began,
                bool ended,
                bool momentumBegan,
                bool momentumEnded);

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
    RnAppKitView *view{nil};
    facebook::react::Tag tag{0};

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
    double lastEmitSeconds{0};
    bool dragging{false};
    // Coasting after the fingers left. Tracked for the same reason `dragging`
    // is: a momentum end that was never begun would leave JavaScript believing
    // a scroll it never heard start has finished.
    bool coasting{false};

    // An animated `scrollTo`. AppKit does its own deceleration, so unlike GTK
    // there is no fling stepper here to borrow -- this brings its own display
    // link, created when a curve starts and invalidated when it ends.
    ScrollSnapConfig snap;
    ScrollAnimation animation;
    id displayLink{nil};
    double animationLastSeconds{0};
  };

  void applyOffset(Entry &entry, double x, double y, bool emitEvent);
  // Recomputes both overlay scrollbars and hands them to the view to draw.
  // Called wherever the offset, the content size or the container size can
  // have changed, which is more places than the offset alone.
  void updateIndicators(const Entry &entry);
  void scrollTowards(Entry &entry, double x, double y, bool animated);
  bool settleOnSnapPoint(Entry &entry, double velocityY);
  void stopAnimation(Entry &entry);

 public:
  // Called from the display link's target, which is an Objective-C object and
  // cannot reach a private member.
  void advanceAnimation(facebook::react::Tag tag, double seconds);

 private:
  void emitScrollEvent(Entry &entry, const char *which);
  void writeStateOffset(const Entry &entry);

  EmitterLookup lookup_;
  std::unordered_map<facebook::react::Tag, Entry> entries_;
  // The object every ScrollView's view points at. One for all of them: it
  // dispatches by tag, so there is nothing per-view to keep in step.
  id scrollTarget_;
};

} // namespace basalt
