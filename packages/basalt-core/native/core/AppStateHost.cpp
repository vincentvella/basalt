#include "AppStateHost.h"

#include <atomic>
#include <mutex>

namespace basalt {

namespace {

std::mutex &lock() {
  static std::mutex value;
  return value;
}

std::atomic<AppStateKind> &state() {
  // Active, not unknown: a host that never reports has still put a window on
  // the screen, and `AppState.currentState` is read before any event can
  // arrive. React Native has an `unknown` for a platform that cannot tell, and
  // all three of these can.
  static std::atomic<AppStateKind> value{AppStateKind::Active};
  return value;
}

std::function<void(AppStateKind)> &listener() {
  static std::function<void(AppStateKind)> value;
  return value;
}

} // namespace

const char *appStateName(AppStateKind state) {
  switch (state) {
    case AppStateKind::Active:
      return "active";
    case AppStateKind::Inactive:
      return "inactive";
    case AppStateKind::Background:
      return "background";
  }
  return "active";
}

AppStateKind appStateFor(bool focused, bool visible) {
  if (!visible) {
    return AppStateKind::Background;
  }
  return focused ? AppStateKind::Active : AppStateKind::Inactive;
}

void notifyAppState(AppStateKind next) {
  std::function<void(AppStateKind)> toCall;
  {
    const std::lock_guard<std::mutex> guard(lock());
    if (state().load(std::memory_order_relaxed) == next) {
      return;
    }
    state().store(next, std::memory_order_relaxed);
    toCall = listener();
  }
  // Outside the lock: it emits into the JavaScript runtime, and holding a lock
  // across that is how a deadlock is built.
  if (toCall) {
    toCall(next);
  }
}

AppStateKind currentAppState() {
  return state().load(std::memory_order_relaxed);
}

void setAppStateListener(std::function<void(AppStateKind)> next) {
  const std::lock_guard<std::mutex> guard(lock());
  listener() = std::move(next);
}

} // namespace basalt
