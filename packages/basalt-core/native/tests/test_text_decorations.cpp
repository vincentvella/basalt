// Which lines a fragment decorates with, and in what colour. See
// core/TextDecorations.h.
//
// Shared because the rules are decisions rather than drawing: a colour with no
// line draws nothing, an unset colour means the text's own, and the style is
// passed through for each toolkit to map to what it has.

#include "TestHarness.h"

#include "TextDecorations.h"

#include <cmath>

using basalt::textDecoration;
using facebook::react::ColorComponents;
using facebook::react::TextAttributes;
using facebook::react::TextDecorationLineType;
using facebook::react::TextDecorationStyle;

namespace {

// Not `near`: see test_font_scaling.cpp for the Windows macro that takes.
bool closeTo(float a, float b) { return std::fabs(a - b) < 0.01F; }

} // namespace

TEST(text_decoration_underline_strikethrough_and_both) {
  TextAttributes attributes;
  attributes.textDecorationLineType = TextDecorationLineType::Underline;
  auto decoration = textDecoration(attributes);
  EXPECT(decoration.has_value());
  EXPECT(decoration.has_value() && decoration->underline);
  EXPECT(decoration.has_value() && !decoration->strikethrough);

  attributes.textDecorationLineType = TextDecorationLineType::Strikethrough;
  decoration = textDecoration(attributes);
  EXPECT(decoration.has_value() && !decoration->underline);
  EXPECT(decoration.has_value() && decoration->strikethrough);

  attributes.textDecorationLineType = TextDecorationLineType::UnderlineStrikethrough;
  decoration = textDecoration(attributes);
  EXPECT(decoration.has_value() && decoration->underline);
  EXPECT(decoration.has_value() && decoration->strikethrough);
}

// The prop that decides whether there is a decoration at all.
TEST(text_decoration_needs_a_line_to_decorate) {
  TextAttributes none;
  EXPECT(!textDecoration(none).has_value());

  none.textDecorationLineType = TextDecorationLineType::None;
  EXPECT(!textDecoration(none).has_value());

  // A colour and a style with no line is a stylesheet styling something it did
  // not ask for, and drawing a line there would be inventing one.
  TextAttributes styledOnly;
  styledOnly.textDecorationStyle = TextDecorationStyle::Dotted;
  styledOnly.textDecorationColor =
      facebook::react::colorFromComponents(ColorComponents{1.0F, 0.0F, 0.0F, 1.0F});
  EXPECT(!textDecoration(styledOnly).has_value());
}

TEST(text_decoration_takes_the_colour_it_was_given) {
  TextAttributes attributes;
  attributes.textDecorationLineType = TextDecorationLineType::Underline;
  attributes.textDecorationColor =
      facebook::react::colorFromComponents(ColorComponents{0.0F, 0.5F, 1.0F, 1.0F});
  const auto decoration = textDecoration(attributes);
  EXPECT(decoration.has_value() && decoration->hasColor);
  EXPECT(decoration.has_value() && closeTo(decoration->blue, 1.0F));
  EXPECT(decoration.has_value() && closeTo(decoration->red, 0.0F));
}

// Unset is not black: it is the text's own colour, which is what both toolkits
// draw when nothing names one.
TEST(text_decoration_an_unset_colour_is_the_texts_own) {
  TextAttributes attributes;
  attributes.textDecorationLineType = TextDecorationLineType::Underline;
  const auto decoration = textDecoration(attributes);
  EXPECT(decoration.has_value() && !decoration->hasColor);
}

TEST(text_decoration_carries_the_style_for_each_host_to_map) {
  TextAttributes attributes;
  attributes.textDecorationLineType = TextDecorationLineType::Underline;
  // Unset is solid, which is CSS's default and React Native's.
  EXPECT(textDecoration(attributes)->style == TextDecorationStyle::Solid);

  for (const auto style : {TextDecorationStyle::Solid, TextDecorationStyle::Double,
                           TextDecorationStyle::Dotted, TextDecorationStyle::Dashed,
                           TextDecorationStyle::Wavy}) {
    attributes.textDecorationStyle = style;
    EXPECT(textDecoration(attributes)->style == style);
  }
}
