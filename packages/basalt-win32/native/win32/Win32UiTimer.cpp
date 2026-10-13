#include "Win32UiTimer.h"

#include <windows.h>

#include "PlatformServices.h"
#include "Win32UiThread.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace basalt {
namespace {

struct Entry {
  std::function<void()> callback;
  PTP_TIMER handle = nullptr;
  bool repeating = false;
};

struct Registry {
  std::mutex mutex;
  std::unordered_map<UiTimerId, Entry> timers;
  UiTimerId next = 1;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

// Closing a thread-pool timer is three calls and the order is not optional:
// stop it, wait out a callback that may already be running on a pool thread,
// and only then release the handle. Skipping the wait is the classic way to
// free a timer out from under its own callback.
//
// Called with the registry lock *not* held: the wait blocks until a pool
// thread leaves `onExpiry`, and `onExpiry` takes that lock.
void closeTimer(PTP_TIMER handle) {
  if (handle == nullptr) {
    return;
  }
  SetThreadpoolTimer(handle, nullptr, 0, 0);
  WaitForThreadpoolTimerCallbacks(handle, TRUE);
  CloseThreadpoolTimer(handle);
}

// On the UI thread, one delay later. Everything about the timer is re-read
// here rather than captured, because the gap between the pool thread posting
// this and the loop running it is exactly when a cancel arrives.
void deliver(UiTimerId id) {
  std::function<void()> callback;
  PTP_TIMER finished = nullptr;
  {
    std::lock_guard<std::mutex> lock(registry().mutex);
    const auto found = registry().timers.find(id);
    if (found == registry().timers.end()) {
      return;
    }
    callback = found->second.callback;
    if (!found->second.repeating) {
      // Out of the table before the callback runs, so that a one-shot which
      // schedules its successor cannot collide with itself, and so that a
      // `cancelUiTimer` on this id from inside the callback is the no-op the
      // header promises.
      finished = found->second.handle;
      registry().timers.erase(found);
    }
  }
  closeTimer(finished);
  callback();
}

VOID CALLBACK onExpiry(PTP_CALLBACK_INSTANCE /*instance*/, PVOID context, PTP_TIMER /*timer*/) {
  const auto id = static_cast<UiTimerId>(reinterpret_cast<std::uintptr_t>(context));
  // Nothing is read here: the pool thread's only job is to say when. Which is
  // also why the id travels rather than a pointer -- the entry may be gone by
  // the time this lands.
  postToUiThread([id] { deliver(id); });
}

} // namespace

UiTimerId scheduleUiTimer(unsigned delayMs, bool repeating, std::function<void()> callback) {
  // See the header: with no loop to post to, a timer has nowhere to arrive,
  // and pretending otherwise would run the callback on a pool thread.
  if (!hasUiThread() || callback == nullptr) {
    return 0;
  }

  UiTimerId id = 0;
  {
    std::lock_guard<std::mutex> lock(registry().mutex);
    id = registry().next++;
  }

  PTP_TIMER handle = CreateThreadpoolTimer(
      onExpiry, reinterpret_cast<PVOID>(static_cast<std::uintptr_t>(id)), nullptr);
  if (handle == nullptr) {
    return 0;
  }

  {
    std::lock_guard<std::mutex> lock(registry().mutex);
    registry().timers[id] = Entry{std::move(callback), handle, repeating};
  }

  // Negative is relative, in hundred-nanosecond units, which is the one piece
  // of this API that reads like a mistake and is not.
  LARGE_INTEGER relative{};
  relative.QuadPart = -static_cast<LONGLONG>(delayMs) * 10000;
  FILETIME due{};
  due.dwLowDateTime = relative.LowPart;
  due.dwHighDateTime = static_cast<DWORD>(relative.HighPart);
  SetThreadpoolTimer(handle, &due, repeating ? static_cast<DWORD>(delayMs) : 0, 0);
  return id;
}

void cancelUiTimer(UiTimerId id) {
  if (id == 0) {
    return;
  }
  PTP_TIMER handle = nullptr;
  {
    std::lock_guard<std::mutex> lock(registry().mutex);
    const auto found = registry().timers.find(id);
    if (found == registry().timers.end()) {
      return;
    }
    handle = found->second.handle;
    registry().timers.erase(found);
  }
  // Outside the lock, because the wait inside waits for `onExpiry` to finish
  // and `onExpiry` wants the lock. A delivery already posted to the loop is
  // harmless: `deliver` looks the id up and finds nothing.
  closeTimer(handle);
}

} // namespace basalt
