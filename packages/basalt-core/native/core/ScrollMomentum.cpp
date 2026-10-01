#include "ScrollMomentum.h"

#include <algorithm>
#include <cmath>

namespace basalt {

namespace {

// Pixels per second below which a fling is over. Roughly a pixel every two
// frames, which is slower than a screen can show moving.
constexpr double kStopSpeed = 30.0;

// A fling is not allowed to take longer than this per step. A frame clock that
// stalls -- a window dragged between monitors, a machine that swapped -- would
// otherwise hand `advance` a whole second and teleport the list.
constexpr double kMaxStepSeconds = 0.1;

double speed(double x, double y) {
  return std::sqrt(x * x + y * y);
}

} // namespace

bool ScrollMomentum::start(double velocityX, double velocityY, double decelerationRate) {
  if (speed(velocityX, velocityY) < kStopSpeed) {
    running_ = false;
    return false;
  }
  velocityX_ = velocityX;
  velocityY_ = velocityY;
  // Clamped rather than trusted: the prop is a float an app can set to
  // anything, and a rate of 1 or more is a fling that never stops.
  decelerationRate_ = std::clamp(decelerationRate, 0.5, 0.9999);
  running_ = true;
  return true;
}

bool ScrollMomentum::advance(double seconds, double &dx, double &dy) {
  dx = 0;
  dy = 0;
  if (!running_) {
    return false;
  }
  const double step = std::clamp(seconds, 0.0, kMaxStepSeconds);

  // The distance covered during the step, at the velocity it started with. A
  // closed-form integral of the decay would be more exact; at sixty steps a
  // second the difference is under a pixel over a whole fling, and this stays
  // readable.
  dx = velocityX_ * step;
  dy = velocityY_ * step;

  const double decay = std::pow(decelerationRate_, step * 1000.0);
  velocityX_ *= decay;
  velocityY_ *= decay;

  if (speed(velocityX_, velocityY_) < kStopSpeed) {
    running_ = false;
    return false;
  }
  return true;
}

void ScrollMomentum::stop() {
  running_ = false;
  velocityX_ = 0;
  velocityY_ = 0;
}

} // namespace basalt
