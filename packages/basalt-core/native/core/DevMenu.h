// React Native's developer menu, for a desktop.
//
// On a phone this is a shake or a hardware key, and on a simulator it is
// Cmd+D. A desktop has neither a shake nor a simulator, so it is a keyboard
// shortcut into a native popup menu -- which is also the first thing in this
// project to use `showMenu`, and the reason that seam exists.
//
// What each item does, and where it goes, is the interesting part, because the
// three actions take three different routes out of here:
//
//   Reload                    React Native's own `DevSettings.reload()`, which
//                             ReactCxxPlatform wires to ReactHost's private
//                             `reloadReactInstance`. There is no public way in
//                             from C++, so this asks JavaScript to make the
//                             call -- through a device event that
//                             `src/overrides/setUpDeveloperTools.js` listens
//                             for. That override is React Native's own file
//                             plus the listener, and it runs from
//                             InitializeCore in every development bundle, so
//                             it needs nothing of the app.
//
//   Toggle Element Inspector  `toggleElementInspector`, which is React Native's
//                             own device event and needs no help from this
//                             package at all: `AppContainer-dev.js` listens for
//                             it and renders the inspector out of ordinary
//                             views, which these hosts already mount.
//
//   Open Debugger             Straight through `ReactHost::openDebugger`, which
//                             is public and asks the dev server to open one.
//
// Nothing here is compiled out of a release build, and it does not need to be:
// the menu is only ever shown by a host running in dev mode, and the actions
// fail quietly without one. What decides is `enableDevMode`, in one place.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace facebook::react {
class ReactHost;
}

namespace basalt {

// What the menu offers, in order. Separate from showing it so that a host can
// put the same list somewhere else -- an application menu bar, when there is
// one -- without this file knowing about menus at all.
struct DevMenuItem {
  std::string label;
  // Shown beside the label. Decoration: the host binds the key that opens the
  // menu, and nothing binds these.
  std::string shortcut;
  // What choosing it does. Called on the main thread.
  std::function<void()> action;
};

// The dev menu for this host. `reactHost` is borrowed and must outlive the
// items; hosts hold both for the life of the process.
std::vector<DevMenuItem> devMenuItems(facebook::react::ReactHost *reactHost);

// Builds the menu, shows it, and runs whichever item was chosen. A no-op
// without a ReactHost, so a host may call it before one exists.
//
// `x` and `y` are in the window's coordinates; negative means "at the pointer",
// which is what a menu opened from the keyboard wants.
void showDevMenu(facebook::react::ReactHost *reactHost, double x = -1.0, double y = -1.0);

// The device event `src/overrides/setUpDeveloperTools.js` listens for, and the
// one action sent through it. Named here so the two sides cannot drift.
inline constexpr const char *kDevMenuEvent = "basaltDevMenu";
inline constexpr const char *kDevMenuReload = "reload";

} // namespace basalt
