// Driving React Native's event beat from the GTK main loop.
//
// Nothing an EventEmitter produces reaches JavaScript on its own.
// `EventQueue::onEnqueue` only sets a flag on the EventBeat; the queue is
// flushed when something calls `RunLoopObserverManager::onRender()`, which
// induces the beat. Until that is wired, touches are enqueued and silently
// never delivered -- which is exactly how this platform behaved before input
// existed, and why it was not noticed earlier.
//
// The observer React Native asks for is `Activity::BeforeWaiting`: run just
// before the loop goes to sleep, having drained whatever else was pending. On
// iOS that is a CFRunLoopObserver, and on Android the Choreographer.
//
// The GLib equivalent is a GSource that does its work in `prepare()` and never
// reports itself ready. `prepare()` runs once per main-loop iteration, before
// the poll, and an idle loop blocks in that poll rather than spinning -- so
// this costs one function call per iteration when busy and nothing at all when
// the application is idle. A tick callback on the frame clock would also work,
// but it would hold the frame clock open and wake the process at display rate
// forever, which is the wrong trade for a desktop app that is usually still.

#pragma once

#include <glib.h>

#include <memory>

namespace facebook::react {
class RunLoopObserverManager;
}

namespace basalt {

// Installs the source on the default main context. Returns the GSource, which
// the caller owns: destroy it with removeRunLoopObserver before the manager it
// refers to goes away.
GSource *installRunLoopObserver(std::shared_ptr<facebook::react::RunLoopObserverManager> manager);

void removeRunLoopObserver(GSource *source);

} // namespace basalt
