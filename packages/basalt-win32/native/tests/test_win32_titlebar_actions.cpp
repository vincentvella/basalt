// The caption's four actions before there is a window.
//
// minimize, toggleMaximize, close and startWindowDrag are what an app-drawn
// header asks for by name. GtkWindowModule and AppKitWindowModule have
// registered them all along; this host registered none of them, so on Windows
// useWindow().minimize() and .close() were silent no-ops -- and useCloseRequest
// could refuse a close and then find that agreeing to it did nothing either.
//
// Now that they are registered, the question this asks is the one a test can
// answer without a desktop: what they do before the host has an HWND. An app
// can call close() from its first render, and the honest answer then is to do
// nothing, not to dereference a null window.
//
// What this deliberately cannot check is the other half -- that a real window
// actually minimises, or that startDrag hands Windows its move loop. Both need
// a window, a message queue and a person's mouse; they belong to the
// end-to-end suite, and saying so here is better than a test that looks like
// it covers them.
//
// Lives in the React-Native-gated source list rather than beside
// test_win32_titlebar.cpp, because Win32TitleBar is compiled into
// basalt_win32_mounting, which only exists when basalt_core does. Its
// neighbour tests pure geometry and needs no such thing.

#include "TestHarness.h"

#include "Win32TitleBar.h"

using basalt::TitleBarStyle;
using basalt::Win32TitleBar;

TEST(caption_actions_do_nothing_when_no_window_is_attached) {
  Win32TitleBar bar;
  const TitleBarStyle before = bar.style();

  bar.minimize();
  bar.toggleMaximize();
  bar.close();
  bar.startDrag();

  // Reaching this line is most of the assertion: each call returned rather
  // than posting to a null window. The style is the one piece of state they
  // could have disturbed on the way, so it is worth saying it did not move.
  EXPECT(bar.style() == before);
}

TEST(caption_actions_are_repeatable_without_a_window) {
  Win32TitleBar bar;

  // Twice, because the guard is a plain early return and an app that polls --
  // a header re-rendering on every frame, say -- makes exactly this call
  // pattern. Nothing here accumulates state, and this is what would notice if
  // one of them started to.
  for (int attempt = 0; attempt < 2; ++attempt) {
    bar.minimize();
    bar.toggleMaximize();
    bar.close();
    bar.startDrag();
  }

  EXPECT(bar.style() == TitleBarStyle::Native);
}
