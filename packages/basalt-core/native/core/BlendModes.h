// What a React Native `mixBlendMode` value is called, in CSS's own words.
//
// ## Why a name rather than a per-host enum
//
// The same reason `cursor` crosses the seam as a keyword, in core/CursorNames.h:
// React Native's `BlendMode` is CSS's list, and so is every host's. GSK has a
// `GskBlendMode` per CSS mode bar one; Core Image has a `CI<Name>BlendMode`
// filter per CSS mode, including that one. Keeping the keyword as the thing that
// crosses means one place says what `mixBlendMode: 'color-dodge'` is called, and
// each view layer answers for its own compositor.
//
// The names are the CSS property's values, hyphenated as CSS writes them, which
// is also how React Native's own `blendModeFromString` spells them.
//
// Header-only and in core because both mounting managers need it and both know
// React Native; the view layers do not, and they take a string.

#pragma once

#include <react/renderer/graphics/BlendMode.h>

namespace basalt {

// The CSS keyword, or nullptr for "nothing to apply", which is `normal`: the
// default, and the only value that asks for ordinary source-over compositing.
inline const char *blendModeName(facebook::react::BlendMode mode) {
  using facebook::react::BlendMode;
  switch (mode) {
    case BlendMode::Normal:
      return nullptr;
    case BlendMode::Multiply:
      return "multiply";
    case BlendMode::Screen:
      return "screen";
    case BlendMode::Overlay:
      return "overlay";
    case BlendMode::Darken:
      return "darken";
    case BlendMode::Lighten:
      return "lighten";
    case BlendMode::ColorDodge:
      return "color-dodge";
    case BlendMode::ColorBurn:
      return "color-burn";
    case BlendMode::HardLight:
      return "hard-light";
    case BlendMode::SoftLight:
      return "soft-light";
    case BlendMode::Difference:
      return "difference";
    case BlendMode::Exclusion:
      return "exclusion";
    case BlendMode::Hue:
      return "hue";
    case BlendMode::Saturation:
      return "saturation";
    case BlendMode::Color:
      return "color";
    case BlendMode::Luminosity:
      return "luminosity";
    case BlendMode::PlusLighter:
      return "plus-lighter";
  }
  return nullptr;
}

} // namespace basalt
