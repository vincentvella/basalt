// The window this process owns, for the parts of the host that need it and are
// not the window module.
//
// `NSApp.keyWindow` and `NSApp.mainWindow` are both nil whenever the app is not
// the active one, and two different things were asking only those: a bounds read
// answered 0x0, and a menu role sent through the responder chain went nowhere.
// Both are what an app looks like when it was launched while something else had
// focus, which is most of the time under a test harness and some of the time on
// a desktop.
//
// So the rule lives in one place. It is GTK's rule, from
// GtkWindowControl.cpp: prefer the active window, fall back to the first visible
// one that is not a panel.

#pragma once

#import <AppKit/AppKit.h>

namespace basalt {

// The app's own window, or nil when it has none yet. Main thread only.
NSWindow *appWindow();

} // namespace basalt
