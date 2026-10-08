// CSS gradients, resolved to the points and stops a toolkit wants.
//
// `backgroundImage` is where React Native put CSS gradients, so
// `linear-gradient(135deg, #f00, #00f)` in a stylesheet arrives as a
// `LinearGradient` with a direction and a list of colour stops, and every host
// ignored it. GSK has a linear-gradient node and Core Graphics has `CGGradient`,
// and both want the same two things: the end points of the gradient line in the
// view's own pixels, and stops with an offset from 0 to 1.
//
// ## Why this is shared and why it is a port
//
// Neither half is obvious and both are specified. The gradient line's end points
// come from CSS's rule that the corners of the box map to the ends of the line,
// which is a perpendicular-bisector construction rather than "the diagonal"; the
// stops come from the colour-stop fixup in css-images-4, which fills in missing
// positions, clamps positions that go backwards, and turns a transition hint into
// a run of interpolated stops. Getting either subtly wrong gives a gradient that
// looks like a gradient.
//
// So both are ported from React Native's own iOS implementation --
// `RCTLinearGradient.mm` and `RCTGradientUtils.mm`, which in turn follow
// Chromium -- and shared between the two hosts, so that the same stylesheet is
// the same picture on both desktops and on iOS. A host that did its own
// arithmetic would be plausible and different.
//
// Header-only, like core/Backface.h: the mounting managers are what include it,
// and they already know React Native.

#pragma once

#include <react/renderer/graphics/Color.h>
#include <react/renderer/graphics/LinearGradient.h>
#include <react/renderer/graphics/ValueUnit.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace basalt {

// One stop, in the terms a toolkit takes: an offset along the gradient line and
// a straight sRGB colour.
struct GradientStop {
  float offset;
  float red;
  float green;
  float blue;
  float alpha;
};

// The gradient line, in the view's own pixels, y pointing down. Both hosts count
// y that way -- GTK natively, AppKit because these views are flipped -- so the
// numbers go straight through.
struct GradientLine {
  float startX;
  float startY;
  float endX;
  float endY;

  float length() const {
    const float dx = endX - startX;
    const float dy = endY - startY;
    return std::sqrt(dx * dx + dy * dy);
  }
};

// The angle a corner keyword means for a box of this size.
//
// Not 45 degrees: `to top right` points at the corner only for a square. CSS
// says the gradient line is perpendicular to the line joining the two
// neighbouring corners, which is what makes a gradient to a corner of a wide box
// shallower than one to a corner of a tall box.
inline float gradientAngleForKeyword(facebook::react::GradientKeyword keyword,
                                     float width,
                                     float height) {
  const double degrees = 180.0 / M_PI;
  switch (keyword) {
    case facebook::react::GradientKeyword::ToTopRight:
      return static_cast<float>(90.0 - std::atan(width / height) * degrees);
    case facebook::react::GradientKeyword::ToBottomRight:
      return static_cast<float>(std::atan(width / height) * degrees + 90.0);
    case facebook::react::GradientKeyword::ToTopLeft:
      return static_cast<float>(std::atan(width / height) * degrees + 270.0);
    case facebook::react::GradientKeyword::ToBottomLeft:
      return static_cast<float>(std::atan(height / width) * degrees + 180.0);
  }
  return 180.0F;
}

// Where the gradient line starts and ends for an angle, in CSS's convention: 0
// degrees points up and they run clockwise, so 90 is to the right.
//
// The construction is the one in the spec and in Chromium: take the corner the
// gradient is heading towards, drop a perpendicular from it onto the gradient
// line through the centre, and that is the end point. The four right angles are
// special-cased because the slope is zero or infinite there.
inline GradientLine linearGradientLineForAngle(float angle, float width, float height) {
  double degrees = std::fmod(static_cast<double>(angle), 360.0);
  if (degrees < 0) {
    degrees += 360.0;
  }

  if (degrees == 0.0) {
    return GradientLine{0.0F, height, 0.0F, 0.0F};
  }
  if (degrees == 90.0) {
    return GradientLine{0.0F, 0.0F, width, 0.0F};
  }
  if (degrees == 180.0) {
    return GradientLine{0.0F, 0.0F, 0.0F, height};
  }
  if (degrees == 270.0) {
    return GradientLine{width, 0.0F, 0.0F, 0.0F};
  }

  const double radians = (90.0 - degrees) * M_PI / 180.0;
  const double slope = std::tan(radians);
  const double perpendicular = -1.0 / slope;
  const double halfWidth = width / 2.0;
  const double halfHeight = height / 2.0;

  double cornerX = halfWidth;
  double cornerY = halfHeight;
  if (degrees < 90.0) {
    cornerX = halfWidth;
    cornerY = halfHeight;
  } else if (degrees < 180.0) {
    cornerX = halfWidth;
    cornerY = -halfHeight;
  } else if (degrees < 270.0) {
    cornerX = -halfWidth;
    cornerY = -halfHeight;
  } else {
    cornerX = -halfWidth;
    cornerY = halfHeight;
  }

  const double intercept = cornerY - perpendicular * cornerX;
  const double endX = intercept / (slope - perpendicular);
  const double endY = perpendicular * endX + intercept;

  return GradientLine{
      static_cast<float>(halfWidth - endX),
      static_cast<float>(halfHeight + endY),
      static_cast<float>(halfWidth + endX),
      static_cast<float>(halfHeight - endY),
  };
}

inline GradientLine linearGradientLine(const facebook::react::LinearGradient &gradient,
                                       float width,
                                       float height) {
  if (std::holds_alternative<facebook::react::Float>(gradient.direction)) {
    const auto angle = static_cast<float>(std::get<facebook::react::Float>(gradient.direction));
    return linearGradientLineForAngle(angle, width, height);
  }
  const auto keyword = std::get<facebook::react::GradientKeyword>(gradient.direction);
  return linearGradientLineForAngle(gradientAngleForKeyword(keyword, width, height), width, height);
}

namespace detail {

// A stop's position as a fraction of the line, or nothing when the stylesheet
// left it out. Points are resolved against the line's length, which is why the
// length has to be known before the stops are.
inline std::optional<float> stopPosition(const facebook::react::ValueUnit &position,
                                         float lineLength) {
  switch (position.unit) {
    case facebook::react::UnitType::Point:
      return lineLength > 0 ? static_cast<float>(position.value) / lineLength : 0.0F;
    case facebook::react::UnitType::Percent:
      return static_cast<float>(position.value) / 100.0F;
    case facebook::react::UnitType::Undefined:
      break;
  }
  return std::nullopt;
}

struct PendingStop {
  facebook::react::SharedColor color;
  std::optional<float> position;
};

inline bool nearly(float a, float b) { return std::fabs(a - b) < 1e-6F; }

// A transition hint -- a position in the stop list with no colour of its own,
// `linear-gradient(red, 40%, blue)` -- becomes a run of ordinary stops whose
// colours follow the curve the spec gives: the weighting is
// t^(ln(0.5)/ln(hint)), which is what puts the midpoint colour at the hint.
//
// Nine stops, which is what React Native's iOS half emits, so the same
// stylesheet produces the same ramp there and here. A hint exactly in the middle
// is dropped instead, that being a plain linear ramp already.
inline std::vector<PendingStop> expandHints(const std::vector<PendingStop> &original) {
  std::vector<PendingStop> stops(original);
  if (original.size() < 3) {
    return stops;
  }
  int offsetIndex = 0;
  for (size_t i = 1; i + 1 < original.size(); i++) {
    if (original[i].color) {
      continue;
    }
    const auto at = static_cast<size_t>(static_cast<int>(i) + offsetIndex);
    if (at < 1 || at + 1 >= stops.size()) {
      continue;
    }
    const float left = stops[at - 1].position.value_or(0.0F);
    const float right = stops[at + 1].position.value_or(1.0F);
    const float hint = stops[at].position.value_or(0.0F);
    const float leftDistance = hint - left;
    const float rightDistance = right - hint;
    const float total = right - left;
    const facebook::react::SharedColor leftColor = stops[at - 1].color;
    const facebook::react::SharedColor rightColor = stops[at + 1].color;

    if (nearly(leftDistance, rightDistance)) {
      stops.erase(stops.begin() + static_cast<long>(at));
      offsetIndex--;
      continue;
    }
    if (nearly(leftDistance, 0.0F)) {
      stops[at].color = rightColor;
      continue;
    }
    if (nearly(rightDistance, 0.0F)) {
      stops[at].color = leftColor;
      continue;
    }

    std::vector<PendingStop> added;
    added.reserve(9);
    if (leftDistance > rightDistance) {
      for (int step = 0; step < 7; step++) {
        added.push_back(PendingStop{
            {}, left + leftDistance * ((7.0F + static_cast<float>(step)) / 13.0F)});
      }
      added.push_back(PendingStop{{}, hint + rightDistance / 3.0F});
      added.push_back(PendingStop{{}, hint + rightDistance * 2.0F / 3.0F});
    } else {
      added.push_back(PendingStop{{}, left + leftDistance / 3.0F});
      added.push_back(PendingStop{{}, left + leftDistance * 2.0F / 3.0F});
      for (int step = 0; step < 7; step++) {
        added.push_back(
            PendingStop{{}, hint + rightDistance * (static_cast<float>(step) / 13.0F)});
      }
    }

    const auto leftComponents = facebook::react::colorComponentsFromColor(leftColor);
    const auto rightComponents = facebook::react::colorComponentsFromColor(rightColor);
    const double logRatio = std::log(0.5) / std::log(leftDistance / total);
    for (auto &stop : added) {
      const float relative = (stop.position.value() - left) / total;
      const double weight = std::pow(static_cast<double>(relative), logRatio);
      if (!std::isfinite(weight)) {
        continue;
      }
      const auto mix = [weight](float from, float to) {
        return static_cast<float>(from + (to - from) * weight);
      };
      stop.color = facebook::react::colorFromComponents(facebook::react::ColorComponents{
          .red = mix(leftComponents.red, rightComponents.red),
          .green = mix(leftComponents.green, rightComponents.green),
          .blue = mix(leftComponents.blue, rightComponents.blue),
          .alpha = mix(leftComponents.alpha, rightComponents.alpha),
      });
    }

    stops.erase(stops.begin() + static_cast<long>(at));
    stops.insert(stops.begin() + static_cast<long>(at), added.begin(), added.end());
    offsetIndex += 8;
  }
  return stops;
}

} // namespace detail

// The stops as a toolkit wants them: every one with an offset, in order.
//
// The fixup in css-images-4, in its own three steps. A first stop with no
// position is at 0 and a last one at 1; a position that goes backwards is pulled
// up to the largest before it, so `red 60%, blue 20%` is a hard edge rather than
// a reversal; and a run with no positions is spread evenly between its
// neighbours. Then the transition hints are expanded.
inline std::vector<GradientStop> resolveGradientStops(
    const std::vector<facebook::react::ColorStop> &stops,
    float lineLength) {
  if (stops.empty()) {
    return {};
  }

  std::vector<detail::PendingStop> pending(stops.size());
  bool anyMissing = false;
  float maxSoFar = detail::stopPosition(stops[0].position, lineLength).value_or(0.0F);

  for (size_t i = 0; i < stops.size(); i++) {
    auto position = detail::stopPosition(stops[i].position, lineLength);
    if (!position.has_value()) {
      if (i == 0) {
        position = 0.0F;
      } else if (i == stops.size() - 1) {
        position = 1.0F;
      }
    }
    if (position.has_value()) {
      const float clamped = std::max(position.value(), maxSoFar);
      pending[i] = detail::PendingStop{stops[i].color, clamped};
      maxSoFar = clamped;
    } else {
      pending[i] = detail::PendingStop{stops[i].color, std::nullopt};
      anyMissing = true;
    }
  }

  if (anyMissing) {
    size_t lastDefined = 0;
    for (size_t i = 1; i < pending.size(); i++) {
      if (!pending[i].position.has_value()) {
        continue;
      }
      const size_t gap = i - lastDefined - 1;
      if (gap > 0 && pending[lastDefined].position.has_value()) {
        const float from = pending[lastDefined].position.value();
        const float step = (pending[i].position.value() - from) / static_cast<float>(gap + 1);
        for (size_t j = 1; j <= gap; j++) {
          pending[lastDefined + j].position = from + step * static_cast<float>(j);
        }
      }
      lastDefined = i;
    }
  }

  std::vector<GradientStop> resolved;
  for (const auto &stop : detail::expandHints(pending)) {
    // A stop React Native could not parse a colour for is dropped rather than
    // painted black, the same judgement the shadows make.
    if (!stop.color || !stop.position.has_value()) {
      continue;
    }
    const auto components = facebook::react::colorComponentsFromColor(stop.color);
    resolved.push_back(GradientStop{
        std::clamp(stop.position.value(), 0.0F, 1.0F),
        components.red,
        components.green,
        components.blue,
        components.alpha,
    });
  }
  return resolved;
}

} // namespace basalt
