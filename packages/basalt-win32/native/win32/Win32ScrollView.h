// <ScrollView> on Win32.
//
// Yoga does the hard part. A ScrollView's Yoga node carries `overflow: scroll`,
// which lets its child exceed the viewport instead of being clamped to it, so
// by the time a mutation arrives the content is already laid out at its full
// size and the only thing missing is the offset.
//
// Structure: a mounted ScrollView has exactly **one** child, a content view.
// React Native's JS wraps the children in an `RCTScrollContentView`, which the
// C++ registry rewrites to a plain `View`, so no extra descriptor is needed and
// nothing here should expect N children.
//
// Scrolling is `RnWin32View::setScrollOffset`, which shifts children while
// painting rather than moving them. The frames the mounting manager wrote stay
// exactly the ones Yoga produced, nothing has to be undone on the next
// mutation, and `hitTest` adds the same offset back on the way down -- so
// picking follows the scroll without knowing what a ScrollView is. GTK reaches
// the same place by shifting children in its layout manager and AppKit by
// moving `bounds.origin`.
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
// All of that is the same on all three desktops, which is why this file reads
// like GtkScrollView.cpp with different event plumbing.
//
// ## What is different here
//
// **Routing.** GTK attaches a `GtkEventControllerScroll` per widget and AppKit
// walks the responder chain; both get "a wheel over a row scrolls the list
// containing it, and a list inside a list scrolls the inner one first" for
// free. A Win32 view has no window of its own and there is no chain to walk, so
// `scrollAt` does it: hit test for the view under the pointer, then walk up
// parents offering the wheel to each ScrollView until one takes it. The first
// that does is the innermost, which is the same answer the other two get.
//
// **Drag phases.** GTK's controller reports scroll-begin and scroll-end, and
// AppKit's NSEvent carries a phase. Win32 has neither: `WM_MOUSEWHEEL` is a
// bare notch with nothing around it. So a run of wheel messages is treated as
// one drag, ended by a short idle timeout -- see `kWheelIdleMs`. Without that
// there would be no `onScrollBeginDrag` or `onScrollEndDrag` on this platform
// at all, and the components that wait for them would wait forever.
//
// What Windows does not have: momentum. `onMomentumScrollBegin` and
// `onMomentumScrollEnd` never fire here, and that is deliberate rather than
// unfinished. A precision touchpad reports `WM_MOUSEWHEEL` with fine deltas and
// no fling velocity -- inertia on Windows belongs to Direct Manipulation, which
// wants to own the viewport and so cannot be used by a platform whose whole
// invariant is that React Native decides where things go. macOS gets momentum
// from the system and GTK gets a velocity this project animates
// (core/ScrollMomentum.h); a wheel supplies neither, and modelling a fling off
// one would report a throw that never happened.

#pragma once

#include "ScrollAnimation.h"
#include "ScrollBounds.h"
#include "ScrollIndicator.h"
#include "ScrollSnap.h"
#include "RnWin32View.h"

#include <react/renderer/components/scrollview/ScrollViewShadowNode.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

namespace basalt {

class Win32ScrollViewManager {
 public:
  // Emitters live in the mounting manager's registry, which is also what owns
  // this, so they are reached through a lookup rather than a second copy.
  using EmitterLookup = std::function<facebook::react::EventEmitter::Shared(facebook::react::Tag)>;

  explicit Win32ScrollViewManager(EmitterLookup lookup);
  ~Win32ScrollViewManager();

  Win32ScrollViewManager(const Win32ScrollViewManager &) = delete;
  Win32ScrollViewManager &operator=(const Win32ScrollViewManager &) = delete;
  Win32ScrollViewManager(Win32ScrollViewManager &&) = delete;
  Win32ScrollViewManager &operator=(Win32ScrollViewManager &&) = delete;

  // Called for every mutation touching a ScrollView. Registers the view the
  // first time, and refreshes the geometry and props after.
  void update(win32::RnWin32View *view, const facebook::react::ShadowView &shadowView);

  void remove(facebook::react::Tag tag);

  // ScrollView's imperative commands: scrollTo, scrollToEnd. Returns false if
  // the command is not one this handles.
  bool dispatchCommand(facebook::react::Tag tag,
                       const std::string &name,
                       const folly::dynamic &args);

  // The wheel, in pixels already: a notch is resolved against kWheelStepPixels
  // by the caller, because only the message knows how many notches it was. The
  // point is in `root`'s coordinates. Returns false when nothing under it
  // scrolls, which is what lets the host leave the message to DefWindowProc.
  bool scrollAt(win32::RnWin32View *root, double x, double y, double dx, double dy);

  // A wheel notch carries no pixel distance of its own, so a step has to be
  // chosen. Roughly three lines of 16pt text, which is what browsers and both
  // other desktops settle on -- and, at 96 DPI, close to what Windows' own
  // three-line default works out to. Public because the host resolves notches
  // into pixels before calling, and the two must use one number.
  //
  // Deliberately not `SPI_GETWHEELSCROLLLINES`. Honouring it would be the more
  // native thing and would make one notch move a different distance here than
  // on the other two desktops, which is the trade this project keeps making the
  // other way: matching the other platform matters more than matching the
  // toolkit's own default.
  static constexpr double kWheelStepPixels = 53.0;

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
    win32::RnWin32View *view{nullptr};
    facebook::react::Tag tag{0};

    // An animated `scrollTo`. Windows models no fling -- a wheel supplies no
    // velocity -- but an animated scroll is given its target, so the argument
    // that rules out momentum does not rule this out. Driven by a thread timer,
    // which needs no window: the scroll manager has a tag, not an HWND.
    ScrollSnapConfig snap;
    ScrollAnimation animation;
    UINT_PTR animationTimer{0};
    unsigned long long animationLastMillis{0};

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
    double lastEmitMs{0};

    bool dragging{false};
    // Bumped on every wheel. An idle timer that wakes and finds a different
    // value knows another notch arrived after it was scheduled, and leaves the
    // drag alone.
    std::uint64_t wheelGeneration{0};
  };

  bool scrollEntry(Entry &entry, double dx, double dy);
  void endWheelDrag(facebook::react::Tag tag, std::uint64_t generation);

  void applyOffset(Entry &entry, double x, double y, bool emitEvent);
  // Recomputes both overlay scrollbars and hands them to the view to draw.
  // Called wherever the offset, the content size or the container size can
  // have changed, which is more places than the offset alone.
  void updateIndicators(const Entry &entry);
  void scrollTowards(Entry &entry, double x, double y, bool animated);
  bool settleOnSnapPoint(Entry &entry, double velocityY);
  void stopAnimation(Entry &entry);
  void advanceAnimation(facebook::react::Tag tag, double seconds);

  // The timer procedure cannot carry a pointer, so it looks the animation up by
  // timer id. Static because `SetTimer` calls a plain function.
  static void CALLBACK onAnimationTimer(HWND hwnd, UINT message, UINT_PTR id, DWORD now);
  void emitScrollEvent(Entry &entry, const char *which);
  void writeStateOffset(const Entry &entry);

  EmitterLookup lookup_;
  std::unordered_map<facebook::react::Tag, Entry> entries_;

  // Cleared by the destructor, and held by every idle timer in flight. A timer
  // that outlives this object -- a surface torn down inside the 150ms after the
  // last notch -- finds the flag false and does nothing, instead of calling
  // through a dangling `this`. The same arrangement Win32ImageLoader uses for
  // the same reason, which is that `postDelayed` has no cancel.
  std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
};

} // namespace basalt
