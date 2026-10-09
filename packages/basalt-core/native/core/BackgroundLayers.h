// Where a `backgroundImage` is painted, how big it is, and how it tiles.
//
// `backgroundSize`, `backgroundPosition` and `backgroundRepeat` reach every host
// and were ignored by all three, on a note that said they had "nothing to act on
// until an image can be a background". That was wrong: CSS treats a gradient as
// an image, so all three apply to the gradients both desktops already draw, and
// React Native's iOS half applies them to exactly those.
//
// ## The two areas, which is the part that is easy to get wrong
//
// CSS gives a background two rectangles. The *positioning area* is what a size
// and a position are resolved against, and it is the padding box --
// `background-origin: padding-box`, which is CSS's default and what iOS passes.
// The *painting area* is what the result is clipped to, and it is the border box
// -- `background-clip: border-box`, also the default. They differ on any view
// with a border, and a gradient that ignores the difference paints under the
// border when it should tile into it.
//
// ## Why this is shared
//
// Every number here is specified and none is obvious: percentages resolve
// against the area and not the image, `right: 25%` is not `left: 25%` because
// the available space is the area minus the image, `round` changes the tile size
// before anything tiles, and `space` changes the step and the anchor but not the
// size. It is ported from `RCTBackgroundImageUtils.mm`, so the same stylesheet is
// the same picture on all three platforms, and asserted against the spec's own
// numbers in core's tests.
//
// Header-only, like core/Gradients.h, and included by the mounting managers.

#pragma once

#include <react/renderer/graphics/BackgroundPosition.h>
#include <react/renderer/graphics/BackgroundRepeat.h>
#include <react/renderer/graphics/BackgroundSize.h>

#include <cmath>
#include <cstddef>
#include <variant>
#include <vector>

namespace basalt {

// One entry of each of the three lists, for an image at `index`.
//
// CSS repeats a list that is shorter than the image list, so the lists are
// indexed modulo their own length -- which is what iOS does. An empty list is
// the prop's default: the whole positioning area, the top left corner, and
// repeat in both axes, all of which come from React Native's own constructors.
inline facebook::react::BackgroundSize backgroundSizeAt(
    const std::vector<facebook::react::BackgroundSize> &sizes, size_t index) {
  if (sizes.empty()) {
    return facebook::react::BackgroundSizeLengthPercentage{};
  }
  return sizes[index % sizes.size()];
}

inline facebook::react::BackgroundPosition backgroundPositionAt(
    const std::vector<facebook::react::BackgroundPosition> &positions, size_t index) {
  if (positions.empty()) {
    return facebook::react::BackgroundPosition{};
  }
  return positions[index % positions.size()];
}

inline facebook::react::BackgroundRepeat backgroundRepeatAt(
    const std::vector<facebook::react::BackgroundRepeat> &repeats, size_t index) {
  if (repeats.empty()) {
    return facebook::react::BackgroundRepeat{};
  }
  return repeats[index % repeats.size()];
}

// A rectangle in the view's own coordinates, y down.
struct BackgroundArea {
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};
};

// One resolved background image: the rectangle the image itself fills, and the
// tile it repeats in.
//
// The tile is what a host repeats: its origin is the first tile's, and its size
// is the period -- the image's size for `repeat` and `round`, the image plus the
// gap for `space`. On an axis that does not repeat, the tile is the painting
// area's extent on that axis, so a host that tiles in both directions at once --
// which is what `gtk_snapshot_push_repeat` does -- gets one row or one column
// without a special case, and a host that loops gets one iteration.
struct BackgroundLayer {
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};

  float tileX{0.0F};
  float tileY{0.0F};
  float tileWidth{0.0F};
  float tileHeight{0.0F};

  bool repeatsX{false};
  bool repeatsY{false};

  bool repeats() const { return repeatsX || repeatsY; }
  // Nothing to paint: an empty area, or a size resolved to nothing.
  bool empty() const { return width <= 0.0F || height <= 0.0F; }
};

namespace detail {

// One axis of `background-size`: a length as itself, a percentage of the area,
// and `auto` as the area. A gradient has no intrinsic size of its own, so CSS's
// answer for `auto` is the positioning area -- which is also why `cover` and
// `contain` come to the same thing here and are left alone, as iOS leaves them.
inline float backgroundExtent(
    const std::variant<std::monostate, facebook::react::ValueUnit> &value, float area) {
  if (!std::holds_alternative<facebook::react::ValueUnit>(value)) {
    return area;
  }
  const auto unit = std::get<facebook::react::ValueUnit>(value);
  if (unit.unit == facebook::react::UnitType::Percent) {
    return unit.value * area / 100.0F;
  }
  return unit.value;
}

// `round` divides the area into a whole number of tiles, nearest to the size
// that was asked for. Applied before the position, because it changes the size.
inline float roundedExtent(float item, float area) {
  if (item <= 0.0F || area <= 0.0F) {
    return item;
  }
  if (std::fmod(area, item) == 0.0F) {
    return item;
  }
  const float divisor = std::round(area / item);
  return divisor > 0.0F ? area / divisor : item;
}

// Where the image sits on one axis. The offsets resolve against the *available*
// space, which is the area minus the image: that is what makes `left: 100%` the
// right edge rather than off the end, and `right: 25%` a quarter in from the
// other side.
//
// **The far edge wins when it is present, which is deliberately not what iOS
// does.** `BackgroundPosition`'s constructor sets `top` and `left` to zero, and
// React Native's prop parser only *writes* the keys the style sent -- so
// `background-position: right 25% bottom 10%` arrives as right and bottom set
// *and* top and left still at their defaults. iOS asks for `top` and `left`
// first, finds the defaults, and draws the image in the corner: the far-edge
// syntax cannot be expressed on that platform. See backlog/upstream.md.
//
// Preferring the far edge is right for every case the JavaScript side can
// produce, because `processBackgroundPosition.js` emits exactly one of each pair
// -- `{top, left}`, `{bottom, right}`, `{top, right}` or `{bottom, left}` -- so a
// `right` in the map means the `left` beside it is React Native's default and
// not the author's.
inline float backgroundOffset(const std::optional<facebook::react::ValueUnit> &near,
                              const std::optional<facebook::react::ValueUnit> &far,
                              float areaOrigin,
                              float available) {
  if (far.has_value()) {
    return areaOrigin + (available - far->resolve(available));
  }
  if (near.has_value()) {
    return areaOrigin + near->resolve(available);
  }
  return areaOrigin;
}

// One axis of the tiling: the period, the first tile's origin, and whether it
// repeats at all. `space` is the one that moves the anchor, because the first
// and last tiles touch the edges of the area with the gaps shared between them.
struct AxisTiling {
  float origin;
  float period;
  bool repeats;
};

inline AxisTiling axisTiling(facebook::react::BackgroundRepeatStyle style,
                             float imageOrigin,
                             float imageExtent,
                             float areaOrigin,
                             float areaExtent,
                             float paintingOrigin,
                             float paintingExtent) {
  using facebook::react::BackgroundRepeatStyle;
  if (style == BackgroundRepeatStyle::NoRepeat || imageExtent <= 0.0F) {
    return AxisTiling{paintingOrigin, paintingExtent, false};
  }

  float period = imageExtent;
  float origin = imageOrigin;

  if (style == BackgroundRepeatStyle::Space) {
    const float fits = std::floor(areaExtent / imageExtent);
    if (fits <= 1.0F) {
      // One tile does not leave a gap to share, so there is nothing to space.
      return AxisTiling{paintingOrigin, paintingExtent, false};
    }
    // The gaps are shared between the tiles, and the first tile is flush with
    // the area rather than wherever the position put it: that is what `space`
    // means, and it is why the position is overridden here.
    period = imageExtent + (areaExtent - fits * imageExtent) / (fits - 1.0F);
    origin = areaOrigin;
  }

  // Back up to the first tile that can reach the painting area, so a host that
  // steps forward covers everything rather than starting mid-pattern.
  const float before = std::ceil((origin - paintingOrigin) / period);
  origin -= before * period;
  return AxisTiling{origin, period, true};
}

} // namespace detail

// The whole thing: an image's rectangle and its tile, from the three props and
// the two areas.
inline BackgroundLayer resolveBackgroundLayer(
    const BackgroundArea &positioning,
    const BackgroundArea &painting,
    const facebook::react::BackgroundSize &size,
    const facebook::react::BackgroundPosition &position,
    const facebook::react::BackgroundRepeat &repeat) {
  BackgroundLayer layer;
  if (positioning.width <= 0.0F || positioning.height <= 0.0F || painting.width <= 0.0F ||
      painting.height <= 0.0F) {
    return layer;
  }

  // The size, which is the area unless the stylesheet said otherwise.
  float width = positioning.width;
  float height = positioning.height;
  if (std::holds_alternative<facebook::react::BackgroundSizeLengthPercentage>(size)) {
    const auto &lengths = std::get<facebook::react::BackgroundSizeLengthPercentage>(size);
    width = detail::backgroundExtent(lengths.x, positioning.width);
    height = detail::backgroundExtent(lengths.y, positioning.height);
  }
  if (repeat.x == facebook::react::BackgroundRepeatStyle::Round) {
    width = detail::roundedExtent(width, positioning.width);
  }
  if (repeat.y == facebook::react::BackgroundRepeatStyle::Round) {
    height = detail::roundedExtent(height, positioning.height);
  }
  if (width <= 0.0F || height <= 0.0F) {
    return layer;
  }

  // The position, against the space the image leaves.
  const float x = detail::backgroundOffset(
      position.left, position.right, positioning.x, positioning.width - width);
  const float y = detail::backgroundOffset(
      position.top, position.bottom, positioning.y, positioning.height - height);

  const detail::AxisTiling across = detail::axisTiling(
      repeat.x, x, width, positioning.x, positioning.width, painting.x, painting.width);
  const detail::AxisTiling down = detail::axisTiling(
      repeat.y, y, height, positioning.y, positioning.height, painting.y, painting.height);

  layer.x = across.repeats ? across.origin : x;
  layer.y = down.repeats ? down.origin : y;
  layer.width = width;
  layer.height = height;
  layer.tileX = across.origin;
  layer.tileY = down.origin;
  layer.tileWidth = across.period;
  layer.tileHeight = down.period;
  layer.repeatsX = across.repeats;
  layer.repeatsY = down.repeats;
  return layer;
}

} // namespace basalt
