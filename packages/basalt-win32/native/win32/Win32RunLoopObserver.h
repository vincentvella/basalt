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
//
// ## "Until it comes up empty" is not enough on its own
//
// That rule assumes the queue eventually comes up empty, and in a real app it
// does not. kino's editor, whose preview is a Skia `<Canvas>`, posts work from
// the JavaScript thread around 250 times a second and invalidates its window
// around 120 -- so `PeekMessage` keeps answering, the drain keeps draining,
// and the beat below it waits for a gap that never comes. The queue is a
// treadmill and the beat is at the end of it.
//
// What that looks like from outside is the worst kind of bug: a button that
// usually works. The touch was hit-tested, the emitter was found, the event
// was enqueued -- and then sat in the queue until some accident of timing let
// the loop reach the bottom. Measured on that app, one scripted tap in three
// did nothing at all, and a real click, which lands later in a run and under
// more load, did nothing far more often than that.
//
// So the drain is bounded: the beat also happens every `kBeatEveryMs` and
// every `kBeatEveryMessages`, whichever comes first. Neither number is a
// tuning parameter to get right -- they are an upper bound on how long an
// event may sit, and the cost of beating early is one pass over an empty
// queue.

#pragma once

#include <functional>
#include <memory>

namespace facebook::react {
class RunLoopObserverManager;
}

namespace basalt {

// How long an event may wait, and how many messages may go by, before the
// beat happens whether or not the queue has emptied. Eight milliseconds is
// half a frame at sixty hertz: an event enqueued at the worst moment still
// reaches JavaScript inside the frame it belongs to.
inline constexpr unsigned kBeatEveryMs = 8;
inline constexpr int kBeatEveryMessages = 64;

// Runs until WM_QUIT, calling `beat` each time the queue empties -- and, on a
// queue that does not empty, at least every `kBeatEveryMs`. Returns the exit
// code from WM_QUIT.
//
// The beat is a parameter rather than the manager itself because the bound
// above is the whole point of this file and a test needs to be able to count
// beats. `beat` may post WM_QUIT, which is how a test ends the loop.
int runMessageLoop(const std::function<void()> &beat);

// The host's own entry point: the loop above, beating React Native's event
// queue. A null manager runs the loop with nothing to beat.
int runMessageLoopWithBeat(std::shared_ptr<facebook::react::RunLoopObserverManager> manager);

} // namespace basalt
