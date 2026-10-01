#include "ScrollAnimation.h"

namespace basalt {

namespace {

// Cubic ease-in-out on [0, 1]. See the header for why this curve.
double eased(double t) {
  if (t < 0.5) {
    return 4.0 * t * t * t;
  }
  const double shifted = -2.0 * t + 2.0;
  return 1.0 - (shifted * shifted * shifted) / 2.0;
}

} // namespace

bool ScrollAnimation::start(double fromX, double fromY, double toX, double toY) {
  if (fromX == toX && fromY == toY) {
    running_ = false;
    return false;
  }
  running_ = true;
  elapsed_ = 0.0;
  fromX_ = fromX;
  fromY_ = fromY;
  toX_ = toX;
  toY_ = toY;
  return true;
}

bool ScrollAnimation::advance(double seconds, double &x, double &y) {
  if (!running_) {
    x = toX_;
    y = toY_;
    return false;
  }

  elapsed_ += seconds;
  if (elapsed_ >= kDurationSeconds) {
    // Exactly the target, not the curve evaluated near 1.0. An app that scrolls
    // to a computed offset and then reads it back should get its own number.
    x = toX_;
    y = toY_;
    running_ = false;
    return false;
  }

  const double progress = eased(elapsed_ / kDurationSeconds);
  x = fromX_ + (toX_ - fromX_) * progress;
  y = fromY_ + (toY_ - fromY_) * progress;
  return true;
}

void ScrollAnimation::stop() {
  running_ = false;
}

} // namespace basalt
