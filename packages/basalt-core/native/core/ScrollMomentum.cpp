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

double scrollMomentumDistance(double velocity, double decelerationRate) {
  const double speed = std::abs(velocity);
  if (speed < kStopSpeed) {
    // A release rather than a fling, which is the same threshold `start`
    // refuses at: nothing coasts from here.
    return 0.0;
  }
  const double rate = std::clamp(decelerationRate, 0.5, 0.9999);
  const double decay = std::log(rate);
  if (decay >= 0.0) {
    return 0.0;
  }

  // Velocity is in pixels per second and the rate is a factor per millisecond,
  // which is the awkwardness React Native's own prop carries. So the integral
  // is over milliseconds and the velocity is divided by a thousand to match:
  //
  //   distance = (v/1000) * integral of rate^t dt, from 0 to the moment the
  //   speed falls to kStopSpeed, which is (rate^T - 1) / ln(rate) with
  //   rate^T = kStopSpeed / |v|.
  const double travelled = (speed / 1000.0) * ((kStopSpeed / speed) - 1.0) / decay;
  return velocity < 0.0 ? -travelled : travelled;
}

void ScrollMomentum::stop() {
  running_ = false;
  velocityX_ = 0;
  velocityY_ = 0;
}

} // namespace basalt
