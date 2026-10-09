// The four iOS shadow props, as the CSS box shadow they describe.
//
// `shadowColor`, `shadowOffset`, `shadowOpacity` and `shadowRadius` are React
// Native's older spelling of a drop shadow, and they are everywhere in real
// apps: every card, sheet and floating button written before `boxShadow`
// existed sets them. They are also iOS-only. Android's view config does not
// carry them, so on Android they do nothing at all and `elevation` is used
// instead.
//
// ## The decision this header is
//
// These hosts honour them, rather than ignoring them as Android does. Two
// reasons. The AppKit host is macOS, where react-native-macos honours them for
// the same reason its iOS parent does -- the props describe a `CALayer` shadow
// and the layer is right there. And a cross-platform app that asks for a shadow
// means the shadow: dropping it on a desktop host would be a deliberate
// difference from what the app's author saw, which is not what "ignored on this
// platform" should mean on a platform that can draw it.
//
// They are converted here rather than drawn, so both hosts get one shadow list
// and neither has a second shadow mechanism. What arrives is a `BoxShadow` with
// CSS's meaning, which both view layers already know how to paint.
//
// ## The two conversions that are not identity
//
// `shadowRadius` is a `CALayer` blur radius, which is a gaussian standard
// deviation; CSS's blur-radius is twice that. So the blur doubles, which is the
// same factor each host already uses in the other direction when it hands a CSS
// blur to its own compositor -- see the box shadow tests on both.
//
// `shadowOpacity` multiplies the colour's own alpha, as `CALayer.shadowOpacity`
// does on top of `shadowColor`. An opacity of zero is iOS's default and means no
// shadow, which is why nothing is produced for it.
//
// Both props keep their defaults from `BaseViewProps` -- offset (0, -3) and
// radius 3 -- so a view that sets only `shadowColor` and `shadowOpacity` gets
// the shadow iOS would give it.

#pragma once

#include <react/renderer/components/view/BaseViewProps.h>
#include <react/renderer/graphics/BoxShadow.h>

#include <optional>
#include <vector>

namespace basalt {

// The shadow the four legacy props ask for, or nothing when they ask for none.
inline std::optional<facebook::react::BoxShadow> legacyShadow(
    const facebook::react::BaseViewProps &props) {
  if (props.shadowOpacity <= 0.0 || !props.shadowColor) {
    return std::nullopt;
  }
  facebook::react::ColorComponents components =
      facebook::react::colorComponentsFromColor(props.shadowColor);
  if (components.alpha <= 0.0F) {
    return std::nullopt;
  }
  components.alpha *= static_cast<float>(props.shadowOpacity);
  if (components.alpha > 1.0F) {
    components.alpha = 1.0F;
  }

  facebook::react::BoxShadow shadow;
  shadow.offsetX = props.shadowOffset.width;
  shadow.offsetY = props.shadowOffset.height;
  // A CALayer blur radius is a standard deviation; CSS's is twice one.
  shadow.blurRadius = props.shadowRadius * 2;
  shadow.spreadDistance = 0;
  shadow.color = facebook::react::colorFromComponents(components);
  shadow.inset = false;
  return shadow;
}

// Every shadow a view casts, in CSS's order: first in the list is the one on
// top. `boxShadow` comes first and the legacy shadow goes behind all of it,
// which is both what iOS does -- the layer's own shadow is behind the extra
// layers React Native adds for `boxShadow` -- and the only order where adding a
// `boxShadow` to an old component cannot be hidden by the shadow it already had.
inline std::vector<facebook::react::BoxShadow> allShadows(
    const facebook::react::BaseViewProps &props) {
  std::vector<facebook::react::BoxShadow> shadows = props.boxShadow;
  if (const std::optional<facebook::react::BoxShadow> legacy = legacyShadow(props)) {
    shadows.push_back(*legacy);
  }
  return shadows;
}

} // namespace basalt
