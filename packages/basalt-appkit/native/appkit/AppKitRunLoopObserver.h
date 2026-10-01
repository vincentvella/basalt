// Driving React Native's event beat from the main run loop.
//
// Nothing an EventEmitter produces reaches JavaScript on its own.
// `EventQueue::onEnqueue` only sets a flag on the EventBeat; the queue is
// flushed when something calls `RunLoopObserverManager::onRender()`, which
// induces the beat. Until that is wired, touches are enqueued and silently
// never delivered.
//
// The observer React Native asks for is `Activity::BeforeWaiting`: run just
// before the loop goes to sleep, having drained whatever else was pending. On
// iOS that is a CFRunLoopObserver, and this is the same thing -- the GTK side
// has to build the equivalent out of a GSource, which is the more interesting
// half of the comment in gtk/GtkRunLoopObserver.h.

#pragma once

#import <CoreFoundation/CoreFoundation.h>

#include <memory>

namespace facebook::react {
class RunLoopObserverManager;
}

namespace basalt {

// Installs the observer on the main run loop. Returns it, and the caller owns
// it: remove it with removeRunLoopObserver before the manager goes away.
CFRunLoopObserverRef installRunLoopObserver(
    std::shared_ptr<facebook::react::RunLoopObserverManager> manager);

void removeRunLoopObserver(CFRunLoopObserverRef observer);

} // namespace basalt
