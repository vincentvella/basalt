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

// Takes it down again, and runs anything still queued on the calling thread so
// that nothing posted is lost. For tests, and this is the one that needs it:
// react-native-skia 3.x records a frame on a thread pool of its own and asks
// the UI thread to present it, and the producer holds a lock across that ask.
// Run inline -- which is what posting with no window installed means on a
// worker, exactly as the note above warns -- it re-enters the producer on the
// thread that already holds the lock, and the suite dies with no message. So
// the canvas test installs a real loop, pumps it, and puts things back for the
// tests after it, which are written against the inline behaviour.
void uninstallUiThread();

} // namespace basalt
