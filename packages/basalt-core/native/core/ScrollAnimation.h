// A programmatic scroll that moves rather than jumps.
//
// `scrollTo({x, y, animated: true})` is one of the most-used things a list does
// -- scroll to top, scroll to the newest message -- and until now the `animated`
// flag was parsed and dropped, so every one of them arrived instantly.
//
// ## Why this is not ScrollMomentum
//
// The file next door is deliberate about it: a fling is velocity with friction
// and *no target*, because where it lands is a consequence of how hard it was
// thrown. This is the opposite -- a target is the only thing given, and what has
// to be invented is the path to it. Exponential friction cannot be used here at
// all: it approaches its destination asymptotically and never arrives.
//
// So: a fixed duration and an easing curve, which is what UIScrollView and
// Android's smooth scroller both do, and what makes two of these in a row look
// deliberate rather than elastic.
//
// ## The curve
//
// Cubic ease-in-out. It starts slow, accelerates, and settles -- and, more
// usefully, its derivative is zero at both ends, so a scroll that begins where
// the last one ended has no visible seam. A linear ramp is the obvious
// alternative and looks mechanical precisely because it starts and stops at full
// speed.
//
// Absolute positions rather than deltas, unlike ScrollMomentum. A fling asks
// "how far since last frame", because it has no idea where it will end; an
// animation knows exactly where it must be at time t, and computing that from
// the start point avoids the drift that accumulating deltas would give over
// ninety frames.

#pragma once

namespace basalt {

class ScrollAnimation {
 public:
  // Long enough to read as movement, short enough not to feel slow. iOS uses
  // about this for `setContentOffset:animated:`; Android's default smooth
  // scroll is a little longer and feels it.
  static constexpr double kDurationSeconds = 0.3;

  // Begins a scroll from one offset to another. A target equal to the start is
  // not an animation and returns false, so a caller can apply the offset and be
  // done rather than running a curve that goes nowhere.
  bool start(double fromX, double fromY, double toX, double toY);

  // Advances by `seconds` and reports where the offset should now be. Returns
  // false on the final step, whose position is exactly the target -- the curve
  // is evaluated at 1.0 rather than approached, so an animated scroll always
  // arrives at the number it was asked for.
  bool advance(double seconds, double &x, double &y);

  void stop();

  bool isRunning() const {
    return running_;
  }

 private:
  bool running_{false};
  double elapsed_{0.0};
  double fromX_{0.0};
  double fromY_{0.0};
  double toX_{0.0};
  double toY_{0.0};
};

} // namespace basalt
