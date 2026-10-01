// The UI thread, as a thing that can be installed and asked about.
//
// `core/PlatformServices.h` declares `postToUiThread` and `isUiThread`, which
// is all the shared half needs. Windows needs two more facts that are not
// portable enough to belong there: something has to *create* the message-only
// window the posting goes through, and callers occasionally have to know
// whether one exists at all.
//
// The second is not a nicety. With no window installed `postToUiThread` runs
// its work inline, which is correct for the harness and the tests -- there is
// one thread and no loop to post to -- and quietly wrong for work posted from a
// worker, because "inline" then means "on the worker". Anything that would
// otherwise hand cross-thread work to a loop that does not exist should ask
// first and stay on one thread instead.

#pragma once

namespace basalt {

// Called by the host from the thread that owns its message loop, before React
// Native starts. Idempotent.
void installUiThread();

// Whether `postToUiThread` will actually marshal, rather than running inline.
bool hasUiThread();

} // namespace basalt
