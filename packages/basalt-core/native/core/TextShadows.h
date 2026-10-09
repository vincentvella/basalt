// `textShadowColor`, `textShadowOffset` and `textShadowRadius`, resolved.
//
// Three props that every pre-CSS React Native title sets and that no host read.
// They are `TextAttributes`, so they arrive per *fragment* -- per `<Text>` inside
// a `<Text>` -- and neither text engine here can draw a different shadow per run:
// GTK appends one `PangoLayout` to the snapshot, and AppKit draws lines with
// `CTLineDraw`, which does not honour an `NSShadow` attribute the way AppKit's
// own text drawing does. So one shadow is taken for the paragraph.
//
// ## Which fragment wins, and why that is written down
//
// The first fragment that asks for a shadow. That is the same rule
// `borderStyle` follows for four sides that can only have one stroke, and it is
// a limit rather than a reading of the spec: `<Text>` with a shadow holding a
// `<Text>` with a different one draws the outer one twice. Recorded in
// backlog/text.md rather than approximated, because the alternative -- drawing
// each run separately -- costs the single-layout arrangement both hosts are
// built on.
//
// ## The radius is a standard deviation, as iOS means it
//
// React Native's iOS half puts `textShadowRadius` into `NSShadow.shadowBlurRadius`,
// which is the `CALayer.shadowRadius` kind of parameter: a gaussian standard
// deviation rather than CSS's blur radius, which is twice one. So AppKit passes
// it to `CGContextSetShadowWithColor` unchanged and GTK doubles it for
// `GskShadow`, which takes CSS's. That is the same conversion, in the same
// direction, as `filter: drop-shadow(...)`; see core/Filters.h.

#pragma once

#include <react/renderer/attributedstring/AttributedString.h>

#include <cmath>
#include <optional>

namespace basalt {

// One text shadow, in the terms both hosts take: an offset, the gaussian's
// standard deviation, and a straight sRGB colour.
struct TextShadow {
  float dx{0.0F};
  float dy{0.0F};
  float standardDeviation{0.0F};
  float red{0.0F};
  float green{0.0F};
  float blue{0.0F};
  float alpha{0.0F};
};

// The shadow a paragraph draws, or nothing.
//
// A shadow needs a colour with some alpha in it: React Native's default is no
// colour at all, and a transparent one is a shadow an app asked to be invisible.
// An offset of nothing and a radius of nothing is also nothing -- a shadow
// exactly behind the glyphs with no blur cannot be seen -- which is worth
// skipping rather than drawing, because both hosts pay for a shadow pass.
inline std::optional<TextShadow> textShadow(
    const facebook::react::AttributedString &attributedString) {
  for (const auto &fragment : attributedString.getFragments()) {
    const auto &attributes = fragment.textAttributes;
    if (!attributes.textShadowColor) {
      continue;
    }
    const auto components =
        facebook::react::colorComponentsFromColor(attributes.textShadowColor);
    if (components.alpha <= 0.0F) {
      continue;
    }

    TextShadow shadow;
    if (attributes.textShadowOffset.has_value()) {
      shadow.dx = static_cast<float>(attributes.textShadowOffset->width);
      shadow.dy = static_cast<float>(attributes.textShadowOffset->height);
    }
    // NaN is how React Native spells "not set" for a Float, so a radius that is
    // not a number is zero rather than a crash in a blur.
    if (!std::isnan(attributes.textShadowRadius)) {
      shadow.standardDeviation = static_cast<float>(attributes.textShadowRadius);
    }
    if (shadow.standardDeviation < 0.0F) {
      shadow.standardDeviation = 0.0F;
    }
    if (shadow.dx == 0.0F && shadow.dy == 0.0F && shadow.standardDeviation == 0.0F) {
      continue;
    }

    shadow.red = components.red;
    shadow.green = components.green;
    shadow.blue = components.blue;
    shadow.alpha = components.alpha;
    return shadow;
  }
  return std::nullopt;
}

} // namespace basalt
