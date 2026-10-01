#include "Win32AnimationChoreographer.h"

#include <windows.h>

#include <chrono>

namespace basalt {
namespace {

// Roughly sixty hertz. SetTimer's resolution is the message queue's -- it
// cannot do better than about ten to sixteen milliseconds and it coalesces --
// so this is an upper bound on the frame rate rather than a promise of one.
// See the header for what would be needed to do it properly.
constexpr UINT kFrameIntervalMs = 16;

// SetTimer's callback carries the timer id and nothing else, so the
// choreographer has to be found from it. One host means one choreographer, and
// a map would be pretending otherwise.
Win32AnimationChoreographer *&currentChoreographer() {
  static Win32AnimationChoreographer *instance = nullptr;
  return instance;
}

void CALLBACK onTimer(HWND, UINT, UINT_PTR, DWORD) {
  if (auto *choreographer = currentChoreographer()) {
    choreographer->onFrame();
  }
}

} // namespace

Win32AnimationChoreographer::~Win32AnimationChoreographer() {
  detach();
}

void Win32AnimationChoreographer::resume() {
  if (running_.exchange(true)) {
    return;
  }
  currentChoreographer() = this;
  // A timer with no window: the callback is delivered as WM_TIMER to the
  // thread's queue and dispatched by the loop, which means it lands on the UI
  // thread like everything else and needs no synchronisation of its own.
  timer_ = static_cast<std::uintptr_t>(SetTimer(nullptr, 0, kFrameIntervalMs, onTimer));
}

void Win32AnimationChoreographer::pause() {
  if (!running_.exchange(false)) {
    return;
  }
  if (timer_ != 0) {
    KillTimer(nullptr, static_cast<UINT_PTR>(timer_));
    timer_ = 0;
  }
}

void Win32AnimationChoreographer::detach() {
  pause();
  if (currentChoreographer() == this) {
    currentChoreographer() = nullptr;
  }
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
