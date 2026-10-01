// BASALT_TEST_KEY: press a key, without a keyboard.
//
// A real key press needs a window the display server considers focused, which an
// automated run does not reliably have on any of the three desktops -- the same
// reason `BASALT_TEST_TAP` exists and enters below the toolkit. So this enters
// after the platform mapping: it builds a `KeyCombination` directly and hands it
// to the host's own delivery, which is everything a real press does except the
// `NSEvent`, keyval or `WM_KEYDOWN` translation.
//
// **What it therefore does not test** is that translation, which is the half most
// likely to be wrong -- the three tables are each other's mirror and each one is
// unit-tested in its own host instead. Worth stating plainly because a scenario
// that passes here says the registry, the focus path and the round trip to
// JavaScript work, and says nothing about whether pressing the left arrow
// produces "ArrowLeft".
//
// ## The syntax
//
//     BASALT_TEST_KEY="z+meta;ArrowLeft;m;A+shift"
//
// Presses separated by `;`, a key and its modifiers separated by `+`. The key is
// a W3C name and is taken literally, so `+` as a key is spelt by putting it
// first. Modifiers are `alt`, `ctrl`, `meta` and `shift`.
//
// Parsed here, once, because three hosts parsing it is three chances to disagree
// about what `z+meta` means -- and disagreeing quietly, since the wrong answer is
// a combination that matches nothing and a scenario that reports a shortcut as
// not firing.

#pragma once

#include "KeyEvents.h"

#include <string>
#include <vector>

namespace basalt {

inline constexpr const char *kTestKeyVar = "BASALT_TEST_KEY";

// The presses `BASALT_TEST_KEY` asks for, in order, or empty when it is unset.
// An entry with no key is skipped rather than refused: one malformed press should
// not lose the rest of a scripted sequence.
std::vector<KeyCombination> scriptedKeyPresses();

// The parser, exposed for its tests. `spec` is one press -- `"z+meta"` -- and the
// result has an empty key when it could not be read.
KeyCombination parseKeyPress(const std::string &spec);

} // namespace basalt
