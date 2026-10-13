#include "Win32RunLoopObserver.h"

#include <windows.h>

#include <react/utils/RunLoopObserverManager.h>

#include <chrono>

namespace basalt {

int runMessageLoop(const std::function<void()> &beat) {
  MSG message{};

  for (;;) {
    // Drain what is queued before beating. Beating between each message would
    // be correct but wasteful, and beating *after* the queue is empty is what
    // "BeforeWaiting" means -- the frame has settled, and whatever the messages
    // produced is now worth flushing to JavaScript in one go.
    //
    // Bounded, though: "after the queue is empty" is a moment that never comes
    // in an app that paints continuously, and an event waiting for it waits
    // forever. See the header for what that cost -- a button that usually
    // worked.
    auto since = std::chrono::steady_clock::now();
    int drained = 0;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        return static_cast<int>(message.wParam);
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);

      // The count is checked first because it is free, and the clock only
      // every `kBeatEveryMessages` messages, so a busy drain does not spend
      // itself in `steady_clock::now`.
      if (++drained < kBeatEveryMessages) {
        continue;
      }
      drained = 0;
      const auto now = std::chrono::steady_clock::now();
      if (now - since < std::chrono::milliseconds(kBeatEveryMs)) {
        continue;
      }
      since = now;
      beat();
    }

    beat();

    // Block until something arrives. MsgWaitForMultipleObjectsEx rather than
    // GetMessage, because the beat above may itself have posted work -- a mount
    // marshalled from the JS thread arrives as a posted message -- and
    // GetMessage would be fine, but this is the form that also takes handles
    // when something later needs to wait on one.
    //
    // MWMO_INPUTAVAILABLE matters and is easy to omit: without it, a message
    // that was already in the queue when the wait begins does not count as an
    // event, and the loop sleeps with work pending until the *next* message
    // arrives. With PeekMessage above having just emptied the queue that is
    // nearly always fine, and "nearly always" is how an app hangs once an hour.
    const DWORD result =
        MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    if (result == WAIT_FAILED) {
      return 1;
    }
  }
}

int runMessageLoopWithBeat(
    std::shared_ptr<facebook::react::RunLoopObserverManager> manager) {
  return runMessageLoop([&manager] {
    if (manager != nullptr) {
      manager->onRender();
    }
  });
}

} // namespace basalt
