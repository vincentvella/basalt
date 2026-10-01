// A fling, after the fingers have gone.
//
// The two desktops divide this differently, and the split is the whole reason
// this file exists rather than one platform's private helper.
//
// macOS does the deceleration itself. An `NSEvent` carries a `momentumPhase`
// alongside its ordinary phase, and the system keeps delivering scroll deltas
// after the fingers lift, tapering off on its own. So the AppKit host models
// nothing: it reports what it is handed, and `onMomentumScrollBegin` and
// `onMomentumScrollEnd` are that report.
//
// GTK does not. `GtkEventControllerScroll` with the kinetic flag emits one
// `decelerate` signal carrying the velocity the gesture ended at, and then
// stops -- the coasting is the application's to run. So there has to be a
// model, and this is it.
//
// ## The model
//
// Exponential friction, which is what UIScrollView, Android's OverScroller and
// every browser use: velocity is multiplied by a constant per millisecond, so
// distance falls off smoothly and the fling has no fixed duration. React Native
// already names the constant -- `decelerationRate`, 0.998 for `'normal'` and
// 0.99 for `'fast'` -- and it is a per-millisecond factor, so the two
// implementations agree on what a fling feels like without either quoting a
// number the other does not have.
//
// It is deliberately not a spring or a fixed-duration curve. Both would need a
// target offset up front, and a fling does not have one: where it lands is a
// consequence of how hard it was thrown.

#pragma once

namespace basalt {

class ScrollMomentum {
 public:
  // React Native's own two, as its `decelerationRate` prop resolves them. The
  // prop is a float, so an app can pass anything between; these are the values
  // `'normal'` and `'fast'` mean.
  static constexpr double kNormalDeceleration = 0.998;
  static constexpr double kFastDeceleration = 0.99;

  // Begins a fling at a velocity in pixels per second, with the same sign
  // convention as a scroll offset: positive moves the content offset up.
  //
  // Returns false, and starts nothing, for a fling too slow to be worth
  // animating. That check is here rather than in a caller because the threshold
  // and the stopping condition are the same number, and a fling that stops on
  // its first frame is worse than one that never started -- it would report a
  // momentum scroll beginning and ending with nothing in between.
  bool start(double velocityX, double velocityY, double decelerationRate);

  // Advances the fling by `seconds` and reports how far the offset should move.
  // Returns false once it has stopped, in which case the deltas are the last
  // step and the caller should apply them and finish.
  bool advance(double seconds, double &dx, double &dy);

  // Stops without reporting a final step. For a fling interrupted by a new
  // gesture, or by the view going away.
  void stop();

  bool isRunning() const {
    return running_;
  }

 private:
  bool running_{false};
  double velocityX_{0};
  double velocityY_{0};
  double decelerationRate_{kNormalDeceleration};
};

} // namespace basalt
