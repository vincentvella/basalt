#include "DisplayChoreographer.h"

#include <utility>

namespace basalt {

void DisplayChoreographer::resume() {
  setWant(kBackend, true);
}

void DisplayChoreographer::pause() {
  setWant(kBackend, false);
}

void DisplayChoreographer::setFramesWanted(bool wanted) {
  setWant(kFrames, wanted);
}

void DisplayChoreographer::setFrameListener(std::function<void()> listener) {
  const std::lock_guard<std::mutex> lock(listenerMutex_);
  listener_ = std::move(listener);
}

bool DisplayChoreographer::running() const {
  return wants_.load(std::memory_order_acquire) != 0;
}

void DisplayChoreographer::setWant(unsigned bit, bool on) {
  const unsigned before = on ? wants_.fetch_or(bit, std::memory_order_acq_rel)
                             : wants_.fetch_and(~bit, std::memory_order_acq_rel);
  const unsigned after = on ? (before | bit) : (before & ~bit);
  // Only a change in "should the clock run" is the host's business.
  if ((before != 0) != (after != 0)) {
    runningChanged();
  }
}

void DisplayChoreographer::deliverFrame(facebook::react::AnimationTimestamp timestamp) {
  const unsigned wants = wants_.load(std::memory_order_acquire);
  if ((wants & kBackend) != 0) {
    onAnimationFrame(timestamp);
  }
  if ((wants & kFrames) != 0) {
    std::function<void()> listener;
    {
      const std::lock_guard<std::mutex> lock(listenerMutex_);
      listener = listener_;
    }
    if (listener) {
      listener();
    }
  }
}

} // namespace basalt
