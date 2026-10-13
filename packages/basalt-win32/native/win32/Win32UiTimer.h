// A delay that arrives, in an app that is busy drawing.
//
// `SetTimer` is the obvious way to wait on Windows and it is not a reliable
// one. `WM_TIMER` is not posted: it is *synthesised* by `GetMessage` and
// `PeekMessage`, and only when the queue holds nothing else at all. It ranks
// below posted messages, below input, and -- the part that matters here --
// below `WM_PAINT`. An app that repaints whenever something changes is fine.
// An app that repaints continuously never gives the queue the empty moment a
// `WM_TIMER` needs, and its timers simply stop.
//
// That is not hypothetical and it is not rare. Measured on kino, an editor
// whose preview is a Skia `<Canvas>`: 120 `WM_PAINT` and 250 marshalled work
// items a second while *idle*, and six `WM_TIMER` in twenty-five seconds. A
// 250ms poll fired four times and then never again; a one-shot quit never
// fired at all, so every automated run of that app had to be killed. The
// other two desktops have no such ordering -- a CFRunLoop timer and a GLib
// source are both ordinary sources with no paint above them -- so this is a
// place where the Windows host was quietly worse than its peers rather than
// differently spelled.
//
// So the waiting happens off the UI thread, on a thread-pool timer, and the
// delivery is `postToUiThread` -- a posted message, which outranks both paint
// and timer, and which is the path every mount already takes. The callback
// still runs on the UI thread, which is the only property any caller wanted
// from `SetTimer` in the first place.
//
// ## Without a UI thread
//
// Nothing is delivered. A host installs the message-only window before React
// Native starts; the unit tests do not, because they have no loop. Under
// `SetTimer` that already meant "never fires" -- a `WM_TIMER` with nobody to
// dispatch it -- and keeping it that way is what lets the suite go on testing
// the code around these timers without growing a scheduler. A test that wants
// one installs a loop and pumps it, as the Skia canvas test does.

#pragma once

#include <cstdint>
#include <functional>

namespace basalt {

// What `scheduleUiTimer` hands back. Zero is "nothing was scheduled", which
// `cancelUiTimer` accepts, so a caller can keep one of these in a field and
// cancel it without asking whether it ever started.
using UiTimerId = std::uint64_t;

// Runs `callback` on the UI thread after `delayMs`, and every `delayMs`
// after that when `repeating`.
//
// The period is a floor rather than a promise, exactly as `SetTimer`'s was:
// the work is posted, so it arrives after whatever the loop is already
// holding. What is different is that it arrives.
UiTimerId scheduleUiTimer(unsigned delayMs, bool repeating, std::function<void()> callback);

// Stops it. Safe on an id that already fired, on one that was never
// scheduled, and on the running timer's own id from inside its callback --
// which is how a one-shot that reschedules itself is written.
//
// Returns having waited for any delivery already in flight on a pool thread,
// so a caller may destroy what the callback captured. It must be called from
// the UI thread, which every caller here is on.
void cancelUiTimer(UiTimerId id);

} // namespace basalt
