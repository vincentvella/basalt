// `spellCheck` and `autoCorrect`, as three states rather than two.
//
// Both are `std::optional<bool>` in React Native's text-input traits, and the
// third state is the one that matters: **unset is not false.** A field that says
// nothing about spell checking wants whatever the platform does by default,
// which on a Mac is to check and on Linux is whatever the input method was
// configured to do. Resolving unset to `false` would turn every ordinary field
// into one that had opted out, and the app never said so.
//
// So the hosts are given three states and leave the toolkit alone for the third.
// That is the same shape the accessible flags use, for the same reason: React
// Native's optionals carry "did not say", and a platform default is a real
// answer rather than a missing one.
//
// ## What each toolkit can honour, which is not the same pair
//
//   - AppKit has both, on an NSTextView: `continuousSpellCheckingEnabled` and
//     `automaticSpellingCorrectionEnabled`. A single-line NSTextField has
//     neither of its own -- the field editor it borrows while focused is the
//     NSTextView that does -- which is why the peer applies them again when a
//     field takes focus.
//   - GTK has spell checking as an input *hint*, `GTK_INPUT_HINT_SPELLCHECK`
//     and its negative, which is a suggestion to the input method rather than
//     an instruction to a checker. It has no hint for autocorrection at all:
//     `WORD_COMPLETION` is completion, which is a different offer.
//
// backlog/platform-macos.md and backlog/text.md record the halves that are not
// a call on each platform.

#pragma once

#include <optional>

namespace basalt {

// What an app asked for, with "did not say" kept.
enum class TextCheckingFlag {
  Unset,
  On,
  Off,
};

inline TextCheckingFlag textCheckingFlag(std::optional<bool> asked) {
  if (!asked.has_value()) {
    return TextCheckingFlag::Unset;
  }
  return *asked ? TextCheckingFlag::On : TextCheckingFlag::Off;
}

// Its name, for a tree dump: nullptr when the app said nothing, so the line is
// absent rather than claiming a default the app did not ask for.
inline const char *textCheckingName(TextCheckingFlag flag) {
  switch (flag) {
    case TextCheckingFlag::On:
      return "on";
    case TextCheckingFlag::Off:
      return "off";
    case TextCheckingFlag::Unset:
      return nullptr;
  }
  return nullptr;
}

} // namespace basalt
