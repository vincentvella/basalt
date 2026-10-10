// Which way a paragraph runs, when the app did not say.
//
// `writingDirection` is three values -- `ltr`, `rtl` and `natural` -- and
// `natural`, which is also the default, means "ask the text". React Native says
// which answer it means: `WritingDirection::Natural` is commented upstream as
// "determines direction using the Unicode Bidi Algorithm rules P2 and P3". P2
// is the direction of the first strong character, where strong means a letter of
// a left-to-right script (L) or of a right-to-left one (R or AL) -- digits,
// punctuation, spaces and symbols are not strong and are skipped -- and P3 is
// that a paragraph with no strong character at all runs left to right.
//
// ## Why this is here rather than asked of each engine
//
// Only one of the three engines answers it. Pango resolves the paragraph
// direction itself when `auto_dir` is on, and `pango_find_base_dir` is P2
// exactly. Core Text resolves it inside a frame, which the AppKit layer does
// not use -- it draws its own lines so that `numberOfLines` can be honoured --
// and DirectWrite does not resolve it at all: `SetReadingDirection` is told, and
// a paragraph of Hebrew with nothing set is laid out left to right.
//
// So two of three need this, and the alternative to one shared answer is one
// real answer and two approximations that differ -- which is the argument
// AppPaths.h makes about the resource path, for the same reason: a paragraph
// that aligns differently on two desktops is worse than one that aligns by a
// rule written down here.
//
// `test_text.cpp` in the GTK suite holds this against `pango_find_base_dir`
// over a list of scripts, so "the rule below is P2" is measured rather than
// claimed, and a disagreement is a failing test rather than a paragraph nobody
// looks at twice.
//
// ## What it does not do
//
// Isolates. P2 skips the characters between an isolate initiator (U+2066..68)
// and its matching PDI, so `<LRI>שלום<PDI>hello` is a left-to-right paragraph
// by the standard and a right-to-left one by this. React Native has no way to
// emit an isolate, an app would have to put one in a string literal, and
// handling them means tracking a stack -- so this is the line, and it is drawn
// here rather than discovered later.

#pragma once

#include <react/renderer/attributedstring/AttributedString.h>

#include <optional>
#include <string_view>

namespace basalt {

// Unicode P2 over UTF-8: the first strong character's direction, or false --
// left to right -- when there is none. Invalid UTF-8 is skipped rather than
// refused; this is a layout decision, not a validator.
bool textStartsRightToLeft(std::string_view utf8);

// The direction a paragraph is laid out in: what the app asked for, or what the
// text says when it asked for `natural` or for nothing.
//
// This is the question every relative alignment needs answered, and the reason
// `textAlign: 'end'` was wrong on all three hosts: `end` is the edge a line
// finishes at, which is the left one here.
bool paragraphIsRightToLeft(std::optional<facebook::react::WritingDirection> direction,
                            std::string_view utf8);

// The paragraph's text, concatenated, which is what the rule above reads. The
// fragments are the paragraph: React Native splits a `<Text>` into one per
// distinct set of attributes, and P2 asks about the whole of it.
std::string paragraphText(const facebook::react::AttributedString &attributedString);

} // namespace basalt
