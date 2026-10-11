// Which language the desktop is in, and which way it reads.
//
// `I18nManager` answered neither until 2026-10-10: `localeIdentifier` was the
// empty string and `isRTL` was false unless an app had called `forceRTL`. The
// reason recorded in CoreModules.cpp was that "deciding it from the system
// locale would need a locale database this does not have", which is true of
// *parsing a locale name* and not of the question: every desktop publishes the
// reading direction directly.
//
//   GTK      `gtk_widget_get_default_direction()`, which GTK itself resolves
//            from the locale -- translators ship "default:RTL" for a
//            right-to-left language and GTK reads it back.
//   AppKit   `+[NSLocale characterDirectionForLanguage:]`, which answers from
//            ICU's own data.
//   Windows  `GetLocaleInfoEx` with `LOCALE_IREADINGLAYOUT`, where 1 is
//            right-to-left.
//
// What this costs if it is wrong is the whole layout mirrored the wrong way,
// which is why none of the three guesses from the language name.
//
// ## Why the layout and not only the flag
//
// `I18nManager.isRTL` is what an app branches on, and React Native expects the
// *layout* to agree: Yoga mirrors `flexDirection: 'row'`, `start`/`end`,
// padding and margin when it is told the direction, and the host is what tells
// it, through `LayoutConstraints::layoutDirection`. All three hosts passed
// `LeftToRight` unconditionally, so even an app calling `forceRTL(true)` and
// reloading -- which is what React Native's own documentation says to do --
// got a left-to-right layout with a flag that claimed otherwise.

#pragma once

#include <optional>
#include <string>

namespace basalt {

struct SystemLocale {
  // BCP 47 or POSIX, as the platform spells it: `en-US` on Windows, `en_US` on
  // the other two. Passed through rather than normalised, because React
  // Native's own platforms do the same -- iOS reports `en_US` and Android
  // `en_US` too -- and an app parsing it is parsing its platform's spelling.
  std::string identifier;
  bool rightToLeft{false};
};

// What the desktop says, asked once at startup by the host. **Called on the UI
// thread**: a toolkit's settings are not thread-safe.
SystemLocale systemLocale();

// What a test asked for, or nothing: `BASALT_TEST_LOCALE=ar-EG` overrides both
// fields, the direction coming from the language rather than from a database --
// the five right-to-left scripts are a short list and a test needs no more
// than that.
//
// Here for the same reason `BASALT_TEST_FONT_SCALE` is in core/FontScaling.h:
// a scenario cannot change the machine's language, and a feature with no test
// that fails when it is removed is not finished.
std::optional<SystemLocale> testLocale();

// Remembered by the host at startup, readable from any thread.
void setSystemLocale(SystemLocale locale);
SystemLocale lastKnownSystemLocale();

// Whether the layout should be mirrored, which is `I18nManager.isRTL`.
//
// React Native's three controls, in its own order of precedence:
//
//   allowRTL(false)   never mirrored, whatever the locale says
//   forceRTL(true)    always mirrored, whatever the locale says
//   otherwise         whatever the locale says
//
// `forceRTL(false)` is not "force left to right": React Native treats it as
// clearing the force, which is why the forced state is an optional here.
bool resolveRightToLeft(bool allowRTL, std::optional<bool> forced, bool systemRightToLeft);

// The answer for this process, from what the host reported and what an app has
// asked for since. Read by the hosts when they build layout constraints and
// by the I18nManager module.
bool layoutIsRightToLeft();

// The app's half, from `I18nManager`.
void setAllowRTL(bool allow);
void setForceRTL(bool force);

} // namespace basalt
