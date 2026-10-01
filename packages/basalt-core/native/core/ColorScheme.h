// Whether the system is in light or dark mode.
//
// The fourth seam a desktop platform owes, after fonts, the text layout manager
// and the component registry. Everything about `Appearance` *except* the answer
// is portable -- the module, the JavaScript-side override, the listener list,
// the event -- and the answer is two functions.
//
// Split this way because the interesting part is not reading the setting, it is
// that React Native's `Appearance.setColorScheme` lets JavaScript override it
// process-wide, and that the override has to win over the system for every
// later read. Putting that in core means neither platform can get it subtly
// different.

#pragma once

#include <functional>
#include <string>

namespace basalt {

enum class ColorScheme { Light, Dark };

// --- What a platform provides ----------------------------------------------

// What the operating system currently says. Called on any thread.
ColorScheme systemColorScheme();

// Starts watching for changes, calling `notifyColorSchemeChanged` when the
// system setting moves. Called once, on the main thread, from the host.
//
// A platform that cannot watch may do nothing: `getColorScheme` still answers
// correctly, and only live switching is lost.
void startObservingColorScheme();

// --- What core provides ------------------------------------------------------

// The scheme an app should use: the override if one was set, the system
// otherwise.
ColorScheme effectiveColorScheme();

// `Appearance.setColorScheme`. Passing nullopt-equivalent -- an empty string
// from JavaScript -- clears the override and hands control back to the system.
void setColorSchemeOverride(const std::string &scheme);
void clearColorSchemeOverride();

// Called by a platform's observer when the system setting changes. Notifies
// every listener, unless an override is in force -- in which case nothing an
// app can see has changed.
void notifyColorSchemeChanged();

// Listeners. The token is what removes one; the module holds one for its life.
using ColorSchemeObserver = std::function<void(ColorScheme)>;
int addColorSchemeObserver(ColorSchemeObserver observer);
void removeColorSchemeObserver(int token);

// "light" or "dark", as React Native's JavaScript spells them.
const char *colorSchemeName(ColorScheme scheme);

} // namespace basalt
