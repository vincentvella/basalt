// Driving React Native's event beat from the message loop.
//
// Nothing an EventEmitter produces reaches JavaScript on its own.
// `EventQueue::onEnqueue` only sets a flag on the EventBeat; the queue is
// flushed when something calls `RunLoopObserverManager::onRender()`, which
// induces the beat. Until that is wired, touches are enqueued and silently
// never delivered -- which looks like an input bug and is not one.
//
// What React Native asks for is `Activity::BeforeWaiting`: run once the loop
// has drained its work and is about to sleep. The three platforms get there
// differently, and Windows is the one where it cannot be *installed*.
//
//   iOS/macOS   a CFRunLoopObserver on kCFRunLoopBeforeWaiting
//   GTK         a GSource whose prepare() does the work and never reports ready
//   Win32       no hook exists, so the loop itself has to be written
//
// A Win32 message loop is `GetMessage`/`DispatchMessage`, and there is no
// callback for "about to block". The equivalent is to stop using `GetMessage`:
// drain with `PeekMessage` until it comes up empty, beat, and only then block
// in `MsgWaitForMultipleObjectsEx`. That is exactly "having drained whatever
// else was pending", and blocking properly rather than polling is what keeps an
// idle app at zero CPU -- which was the objection to `gtk_widget_add_tick_callback`
// recorded in `docs/DECISIONS.md`, and applies here for the same reason.
//
// So this is a loop rather than an observer, and the host calls it instead of
// writing its own.

#pragma once

#include <memory>

namespace facebook::react {
class RunLoopObserverManager;
}

namespace basalt {

// Runs until WM_QUIT, inducing the beat each time the queue empties. Returns
// the exit code from WM_QUIT.
int runMessageLoopWithBeat(std::shared_ptr<facebook::react::RunLoopObserverManager> manager);

} // namespace basalt
