#include "Win32RunLoopObserver.h"

#include <windows.h>

#include <react/utils/RunLoopObserverManager.h>

namespace basalt {

int runMessageLoopWithBeat(
    std::shared_ptr<facebook::react::RunLoopObserverManager> manager) {
  MSG message{};
  for (;;) {
    // Drain everything queued before beating. Beating between each message
    // would be correct but wasteful, and beating *after* the queue is empty is
    // what "BeforeWaiting" means -- the frame has settled, and whatever the
    // messages produced is now worth flushing to JavaScript in one go.
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      if (message.message == WM_QUIT) {
        return static_cast<int>(message.wParam);
      }
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }

    if (manager != nullptr) {
      manager->onRender();
    }

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

} // namespace basalt
