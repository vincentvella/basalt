// `textDecorationLine`, `textDecorationColor` and `textDecorationStyle`.
//
// The line was read on both hosts and the other two were not, so an underline
// was always a solid one in the text's own colour. What is shared here is the
// resolution rather than the drawing: which lines are on, what colour they are
// when an app names one, and the style it asked for. Each toolkit then maps the
// style to its own vocabulary, and they do not have the same one.
//
// ## What each toolkit can draw, which is not the same list
//
// React Native has five styles -- solid, double, dotted, dashed and wavy -- and
// neither desktop has all five:
//
//   - Core Text takes a style and a *pattern* in one bitmask, so
//     `NSUnderlineStyleSingle | NSUnderlineStylePatternDot` is a dotted
//     underline and `...PatternDash` a dashed one. It has no wavy.
//   - Pango's underline is an enum with no patterns in it:
//     `PANGO_UNDERLINE_SINGLE`, `DOUBLE` and `ERROR`, the last being the wavy
//     red one a spell checker draws. So dotted and dashed have no value, and
//     strikethrough is a boolean with no style at all.
//
// So each host draws what it has and falls back to a single line otherwise,
// which backlog/text.md records with the call that would close the gap: drawing
// the line in the snapshot rather than asking the text engine for it.
//
// Falling back rather than refusing is the choice worth stating. A dotted
// underline drawn solid is wrong in a way a reader can see; a dotted underline
// not drawn at all is wrong in a way that looks like the prop being ignored,
// which is what the three props were doing before any of this.

#pragma once

#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/graphics/Color.h>

#include <optional>

namespace basalt {

// One fragment's decoration, in the terms both hosts take.
struct TextDecoration {
  bool underline{false};
  bool strikethrough{false};
  facebook::react::TextDecorationStyle style{facebook::react::TextDecorationStyle::Solid};
  // Unset means the text's own colour, which is what both toolkits do when no
  // decoration colour is given, and is React Native's default: iOS sets
  // `NSUnderlineColorAttributeName` only when `textDecorationColor` is there.
  bool hasColor{false};
  float red{0.0F};
  float green{0.0F};
  float blue{0.0F};
  float alpha{0.0F};
};

// What a fragment decorates, or nothing.
//
// `textDecorationLine` is the prop that decides whether there is a decoration at
// all: a colour or a style with no line is an app styling something it did not
// ask for, and drawing a line there would be inventing one.
inline std::optional<TextDecoration> textDecoration(
    const facebook::react::TextAttributes &textAttributes) {
  using facebook::react::TextDecorationLineType;

  if (!textAttributes.textDecorationLineType.has_value()) {
    return std::nullopt;
  }

  TextDecoration decoration;
  switch (*textAttributes.textDecorationLineType) {
    case TextDecorationLineType::None:
      return std::nullopt;
    case TextDecorationLineType::Underline:
      decoration.underline = true;
      break;
    case TextDecorationLineType::Strikethrough:
      decoration.strikethrough = true;
      break;
    case TextDecorationLineType::UnderlineStrikethrough:
      decoration.underline = true;
      decoration.strikethrough = true;
      break;
  }

  if (textAttributes.textDecorationStyle.has_value()) {
    decoration.style = *textAttributes.textDecorationStyle;
  }

  if (textAttributes.textDecorationColor) {
    const auto components =
        facebook::react::colorComponentsFromColor(textAttributes.textDecorationColor);
    decoration.hasColor = true;
    decoration.red = components.red;
    decoration.green = components.green;
    decoration.blue = components.blue;
    decoration.alpha = components.alpha;
  }

  return decoration;
}

} // namespace basalt
