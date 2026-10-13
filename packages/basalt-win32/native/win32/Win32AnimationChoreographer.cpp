#include "Win32AnimationChoreographer.h"

#include "Win32UiTimer.h"

#include <chrono>

namespace basalt {
namespace {

// Roughly sixty hertz. The delay is a floor rather than a promise -- the frame
// is posted to the loop and arrives behind whatever the loop is already
// holding -- so this is an upper bound on the frame rate. See the header for
// what would be needed to do it properly.
constexpr unsigned kFrameIntervalMs = 16;

} // namespace

Win32AnimationChoreographer::~Win32AnimationChoreographer() {
  detach();
}

void Win32AnimationChoreographer::resume() {
  if (running_.exchange(true)) {
    return;
  }
  // Not `SetTimer`. A sixty-hertz WM_TIMER is exactly the arrangement that
  // stops working in an app that repaints continuously, which is to say in
  // every app with an animation worth running. See win32/Win32UiTimer.h.
  timer_ = scheduleUiTimer(kFrameIntervalMs, true, [this] { onFrame(); });
}

void Win32AnimationChoreographer::pause() {
  if (!running_.exchange(false)) {
    return;
  }
  // Before this returns, nothing of ours is still in flight: `cancelUiTimer`
  // waits out a delivery already on a pool thread, which is what makes the
  // `this` captured above safe to destroy afterwards.
  cancelUiTimer(timer_);
  timer_ = 0;
}

void Win32AnimationChoreographer::detach() {
  pause();
}

void Win32AnimationChoreographer::onFrame() {
  if (!running_.load()) {
    return;
  }
  // A steady clock rather than the wall clock: an animation must not jump when
  // the machine's time is corrected or the clocks change.
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  const auto milliseconds =
      std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(now);
  onAnimationFrame(static_cast<facebook::react::AnimationTimestamp>(milliseconds.count()));
}

} // namespace basalt
