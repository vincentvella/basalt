// `writingDirection`, as a name both hosts print.
//
// The prop decides which edge a paragraph starts from, and the two text engines
// take it in their own terms: a Pango context's base direction, an
// `NSParagraphStyle`'s `baseWritingDirection`. What is shared is only the word
// the tree dump reports, which exists so the two dumps compare line by line.
//
// `natural` is printed rather than left out. Asking for the Unicode algorithm's
// answer is not the same as saying nothing: a nested `<Text>` inherits the
// enclosing paragraph's direction when nothing is said, and the algorithm's
// answer when `natural` is.

#pragma once

#include <react/renderer/attributedstring/AttributedString.h>

#include <optional>

namespace basalt {

// The direction the paragraph asked for, from the first fragment that has one.
// React Native resolves a paragraph's attributes onto every fragment in it, so
// the first is the paragraph's; a nested `<Text>` with its own direction is a
// limit this shares with the text shadow, and backlog/text.md records it.
inline std::optional<facebook::react::WritingDirection> writingDirection(
    const facebook::react::AttributedString &attributedString) {
  for (const auto &fragment : attributedString.getFragments()) {
    if (fragment.textAttributes.baseWritingDirection.has_value()) {
      return fragment.textAttributes.baseWritingDirection;
    }
  }
  return std::nullopt;
}

// Its name, or nullptr when the app asked for nothing.
inline const char *writingDirectionName(
    std::optional<facebook::react::WritingDirection> direction) {
  if (!direction.has_value()) {
    return nullptr;
  }
  switch (*direction) {
    case facebook::react::WritingDirection::Natural:
      return "natural";
    case facebook::react::WritingDirection::LeftToRight:
      return "ltr";
    case facebook::react::WritingDirection::RightToLeft:
      return "rtl";
  }
  return nullptr;
}

} // namespace basalt
