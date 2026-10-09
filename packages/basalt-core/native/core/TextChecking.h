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

#include <react/renderer/components/iostextinput/primitives.h>

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

// `autoCapitalize` and `keyboardType`, which are the other half of the same
// entry and the other way round: GTK can express both and macOS neither.
//
// Both are plain enums rather than optionals, so there is no "did not say"
// here: React Native's default for capitalisation **is** sentences, which is
// what iOS does, and a host that treated the default as "no opinion" would be
// quietly disagreeing with every other platform. So the hint is set either way
// and the comment above each mapping says what it means.
//
// Nothing resolves them in core beyond naming them for the dump: a GTK input
// purpose and a GTK input hint are GTK's vocabulary, and AppKit has no
// vocabulary to translate into. What is shared is the word both dumps print.

// `autoCapitalize`, as React Native spells it in JavaScript.
inline const char *autoCapitalizeName(
    facebook::react::AutocapitalizationType type) {
  switch (type) {
    case facebook::react::AutocapitalizationType::None:
      return "none";
    case facebook::react::AutocapitalizationType::Words:
      return "words";
    case facebook::react::AutocapitalizationType::Sentences:
      return "sentences";
    case facebook::react::AutocapitalizationType::Characters:
      return "characters";
  }
  return "sentences";
}

// `keyboardType`, as React Native spells it. The iOS-only and Android-only
// members are named as they are written in a stylesheet rather than collapsed,
// because a dump that said "default" for four different asks would hide which
// one an app made.
inline const char *keyboardTypeName(facebook::react::KeyboardType type) {
  switch (type) {
    case facebook::react::KeyboardType::Default:
      return "default";
    case facebook::react::KeyboardType::EmailAddress:
      return "email-address";
    case facebook::react::KeyboardType::Numeric:
      return "numeric";
    case facebook::react::KeyboardType::PhonePad:
      return "phone-pad";
    case facebook::react::KeyboardType::NumberPad:
      return "number-pad";
    case facebook::react::KeyboardType::DecimalPad:
      return "decimal-pad";
    case facebook::react::KeyboardType::ASCIICapable:
      return "ascii-capable";
    case facebook::react::KeyboardType::NumbersAndPunctuation:
      return "numbers-and-punctuation";
    case facebook::react::KeyboardType::URL:
      return "url";
    case facebook::react::KeyboardType::NamePhonePad:
      return "name-phone-pad";
    case facebook::react::KeyboardType::Twitter:
      return "twitter";
    case facebook::react::KeyboardType::WebSearch:
      return "web-search";
    case facebook::react::KeyboardType::ASCIICapableNumberPad:
      return "ascii-capable-number-pad";
    case facebook::react::KeyboardType::VisiblePassword:
      return "visible-password";
  }
  return "default";
}

} // namespace basalt
