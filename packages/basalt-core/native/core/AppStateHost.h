// `AppState`, which told every desktop app nothing at all.
//
// ReactCxxPlatform provides an `AppState` module and every method of it is
// empty: `getConstants` answers `initialAppState: ""`, `getCurrentAppState`
// calls neither of its callbacks, and `addListener` does nothing. There is a
// `// TODO: T160890586 Wire up AppState implementation to use real data` where
// the work would go. So `AppState.currentState` was the empty string -- not
// `'active'`, not `'unknown'` -- and `AppState.addEventListener('change')`
// never fired, on all three hosts.
//
// That is an API apps use for real work: pausing a poll while the app is not
// in front, refetching when it comes back, saving a draft on the way out.
//
// ## What the three states mean on a desktop
//
// React Native's own are iOS's: `active`, `inactive` and `background`. A phone
// has no focus and no windows, so the mapping has to be chosen, and it is the
// one react-native-macos uses:
//
//   active      the application is frontmost
//   inactive    it is running and visible, and something else has focus
//   background  nothing of it is on screen: hidden, or every window minimised
//
// `inactive` on iOS is a transition -- a phone call arriving over the app --
// and the desktop analogue is "another window is in front", which is the same
// thing an app wants it for: do less, but stay ready.
//
// The mapping is here rather than in each host because all three decide it
// from the same two facts, and only *how they learn them* differs: an
// `is-active` notification on GTK, `NSApplicationDidBecomeActive` and its
// siblings on AppKit, `WM_ACTIVATEAPP` on Windows.

#pragma once

#include <functional>

namespace basalt {

enum class AppStateKind {
  Active,
  Inactive,
  Background,
};

// React Native's own spelling, which is what crosses into JavaScript.
const char *appStateName(AppStateKind state);

// The state those two facts mean. Invisible wins over focused: a minimised
// window that somehow still holds focus is not something an app should treat
// as frontmost.
AppStateKind appStateFor(bool focused, bool visible);

// Told by the host whenever either fact changes, on the UI thread. Stores the
// result and calls the listener below, but only when it is different: React
// Native's `AppState` re-emits whatever it is handed, and an app that refetches
// on every `active` should not refetch because a window moved.
void notifyAppState(AppStateKind state);

// What was last reported, readable from any thread -- a TurboModule method runs
// on the JavaScript thread. `Active` before a host has said anything, which is
// what an application that has just started and drawn a window is.
AppStateKind currentAppState();

// The module's half. Replacing a listener drops the previous one; passing
// nothing removes it, which is what the module's destructor does.
void setAppStateListener(std::function<void(AppStateKind)> listener);

} // namespace basalt
