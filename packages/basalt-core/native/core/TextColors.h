// A fragment's foreground and background, with `opacity` applied.
//
// Three rules that were spelled twice, once per host, and one of them was not
// spelled at all:
//
//   - the default foreground is **opaque black**, which is React Native's and
//     not the platform's. AppKit's `labelColor` is white in dark mode and would
//     make default text invisible on a light background; matching React Native
//     is what keeps the same app looking the same on all three desktops.
//   - `opacity` multiplies the alpha of both colours. No host read it, so a
//     `<Text style={{opacity: 0.5}}>` inside another `<Text>` was fully opaque.
//   - a background with no colour is not transparent black, it is *nothing*:
//     drawing it would paint a box behind text that asked for none.
//
// Copied from upstream's `RCTEffectiveForegroundColorFromTextAttributes` and its
// background counterpart, including the detail that opacity applies to the
// default black as well: an app that sets opacity and no colour still gets
// translucent text.

#pragma once

#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/graphics/Color.h>

#include <cmath>
#include <optional>

namespace basalt {

// A straight sRGB colour, which is what both toolkits want handing over.
struct TextColor {
  float red{0.0F};
  float green{0.0F};
  float blue{0.0F};
  float alpha{1.0F};
};

// `opacity`, as a factor. NaN is React Native's "unset" and means 1; a value
// outside 0..1 is clamped, a stylesheet being able to ask for either.
inline float textOpacity(const facebook::react::TextAttributes &textAttributes) {
  if (std::isnan(textAttributes.opacity)) {
    return 1.0F;
  }
  const float opacity = static_cast<float>(textAttributes.opacity);
  if (opacity < 0.0F) {
    return 0.0F;
  }
  return opacity > 1.0F ? 1.0F : opacity;
}

// The colour the glyphs are drawn in.
inline TextColor textForegroundColor(
    const facebook::react::TextAttributes &textAttributes) {
  TextColor color;
  if (textAttributes.foregroundColor) {
    const auto components =
        facebook::react::colorComponentsFromColor(textAttributes.foregroundColor);
    color.red = components.red;
    color.green = components.green;
    color.blue = components.blue;
    color.alpha = components.alpha;
  }
  color.alpha *= textOpacity(textAttributes);
  return color;
}

// The colour behind them, or nothing.
inline std::optional<TextColor> textBackgroundColor(
    const facebook::react::TextAttributes &textAttributes) {
  if (!textAttributes.backgroundColor) {
    return std::nullopt;
  }
  const auto components =
      facebook::react::colorComponentsFromColor(textAttributes.backgroundColor);
  TextColor color;
  color.red = components.red;
  color.green = components.green;
  color.blue = components.blue;
  color.alpha = components.alpha * textOpacity(textAttributes);
  return color;
}

} // namespace basalt
