// Where a background image goes and how it tiles. See core/BackgroundLayers.h.
//
// Every number is specified and every wrong answer still paints a gradient, so
// these are the spec's own cases: percentages against the area rather than the
// image, the far edges against the space the image leaves, `round` changing the
// size before anything tiles, and `space` changing the step and the anchor but
// not the size.
//
// The areas are deliberately different throughout -- a 100x100 border box with a
// 10pt border, so the padding box is (10,10,80,80) -- because an implementation
// that used one for both is the mistake this file exists to catch, and it is
// invisible on a view with no border.

#include "TestHarness.h"

#include "BackgroundLayers.h"

#include <sstream>

using basalt::BackgroundArea;
using basalt::BackgroundLayer;
using basalt::resolveBackgroundLayer;
using facebook::react::BackgroundPosition;
using facebook::react::BackgroundRepeat;
using facebook::react::BackgroundRepeatStyle;
using facebook::react::BackgroundSize;
using facebook::react::BackgroundSizeLengthPercentage;
using facebook::react::UnitType;
using facebook::react::ValueUnit;

namespace {

const BackgroundArea kPainting{0.0F, 0.0F, 100.0F, 100.0F};
const BackgroundArea kPositioning{10.0F, 10.0F, 80.0F, 80.0F};

// React Native's defaults, which are what an app that sets none of the three
// props sends: auto size, the top left corner, and repeat in both axes.
BackgroundSize autoSize() { return BackgroundSizeLengthPercentage{}; }

BackgroundSize sized(float x, UnitType xUnit, float y, UnitType yUnit) {
  BackgroundSizeLengthPercentage size;
  size.x = ValueUnit{x, xUnit};
  size.y = ValueUnit{y, yUnit};
  return size;
}

BackgroundPosition at(float x, UnitType xUnit, float y, UnitType yUnit) {
  BackgroundPosition position;
  position.left = ValueUnit{x, xUnit};
  position.top = ValueUnit{y, yUnit};
  return position;
}

BackgroundRepeat repeating(BackgroundRepeatStyle x, BackgroundRepeatStyle y) {
  BackgroundRepeat repeat;
  repeat.x = x;
  repeat.y = y;
  return repeat;
}

bool near(float a, float b) { return std::fabs(a - b) < 0.01F; }

} // namespace

// The defaults, which is the case every view with a gradient and nothing else
// goes through. The image fills the *padding* box, not the border box, which is
// the difference from what this platform drew before the props were read.
TEST(background_the_defaults_fill_the_padding_box) {
  const BackgroundLayer layer = resolveBackgroundLayer(
      kPositioning, kPainting, autoSize(), BackgroundPosition{}, BackgroundRepeat{});

  EXPECT(near(layer.width, 80.0F));
  EXPECT(near(layer.height, 80.0F));
  // Repeat is React Native's default, so the tile is the image and the pattern
  // is backed up far enough to cover the border box it is clipped to.
  EXPECT(layer.repeatsX);
  EXPECT(layer.repeatsY);
  EXPECT(near(layer.tileWidth, 80.0F));
  EXPECT(near(layer.tileHeight, 80.0F));
  EXPECT(near(layer.tileX, -70.0F));
  EXPECT(near(layer.tileY, -70.0F));
  // The image is drawn at the tile's origin, the tile being the image itself.
  EXPECT(near(layer.x, layer.tileX));
  EXPECT(near(layer.y, layer.tileY));
}

// A size in points and a size in percent, where the percentage is of the
// positioning area and not of the painting area: 50% of an 80pt padding box is
// 40, where 50% of the 100pt border box would be 50.
TEST(background_a_size_resolves_against_the_positioning_area) {
  const BackgroundLayer points = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(30.0F, UnitType::Point, 20.0F, UnitType::Point),
      BackgroundPosition{},
      repeating(BackgroundRepeatStyle::NoRepeat, BackgroundRepeatStyle::NoRepeat));
  EXPECT(near(points.width, 30.0F));
  EXPECT(near(points.height, 20.0F));

  const BackgroundLayer percent = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(50.0F, UnitType::Percent, 25.0F, UnitType::Percent),
      BackgroundPosition{},
      repeating(BackgroundRepeatStyle::NoRepeat, BackgroundRepeatStyle::NoRepeat));
  EXPECT(near(percent.width, 40.0F));
  EXPECT(near(percent.height, 20.0F));
}

// `cover` and `contain` come to the area, which is what CSS says for an image
// with no intrinsic size -- a gradient -- and is what iOS does with them.
TEST(background_cover_and_contain_are_the_area_for_a_gradient) {
  for (const auto keyword :
       {facebook::react::BackgroundSizeKeyword::Cover, facebook::react::BackgroundSizeKeyword::Contain}) {
    const BackgroundLayer layer = resolveBackgroundLayer(
        kPositioning, kPainting, keyword, BackgroundPosition{}, BackgroundRepeat{});
    EXPECT(near(layer.width, 80.0F));
    EXPECT(near(layer.height, 80.0F));
  }
}

// The position, against the space the image leaves rather than the whole area.
// A 30pt image in an 80pt area has 50pt of slack, so 50% of it is 25 -- not 40.
TEST(background_a_position_resolves_against_the_space_the_image_leaves) {
  const BackgroundLayer layer = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(30.0F, UnitType::Point, 30.0F, UnitType::Point),
      at(50.0F, UnitType::Percent, 100.0F, UnitType::Percent),
      repeating(BackgroundRepeatStyle::NoRepeat, BackgroundRepeatStyle::NoRepeat));

  EXPECT(near(layer.x, 10.0F + 25.0F));
  // 100% of the slack is flush with the far edge of the padding box.
  EXPECT(near(layer.y, 10.0F + 50.0F));
}

// The far edges are not the near ones. `right: 25%` is a quarter of the slack in
// from the right, which is three quarters along, and an implementation that
// resolved both the same way would be right exactly half the time.
TEST(background_the_far_edges_are_measured_from_the_far_side) {
  BackgroundPosition position;
  position.right = ValueUnit{25.0F, UnitType::Percent};
  position.bottom = ValueUnit{10.0F, UnitType::Point};

  const BackgroundLayer layer = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(40.0F, UnitType::Point, 40.0F, UnitType::Point),
      position,
      repeating(BackgroundRepeatStyle::NoRepeat, BackgroundRepeatStyle::NoRepeat));

  // 40pt of slack, a quarter of it from the right: 10 + 30.
  EXPECT(near(layer.x, 40.0F));
  // 10pt in from the bottom of the padding box: 10 + 40 - 10.
  EXPECT(near(layer.y, 40.0F));
}

// `no-repeat` leaves the tile as the painting area, so a host that repeats in
// both axes at once draws it exactly once.
TEST(background_no_repeat_is_one_tile_over_the_painting_area) {
  const BackgroundLayer layer = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(30.0F, UnitType::Point, 30.0F, UnitType::Point),
      BackgroundPosition{},
      repeating(BackgroundRepeatStyle::NoRepeat, BackgroundRepeatStyle::NoRepeat));

  EXPECT(!layer.repeatsX);
  EXPECT(!layer.repeatsY);
  EXPECT(near(layer.tileX, 0.0F));
  EXPECT(near(layer.tileY, 0.0F));
  EXPECT(near(layer.tileWidth, 100.0F));
  EXPECT(near(layer.tileHeight, 100.0F));
  // And the image stays where the position put it.
  EXPECT(near(layer.x, 10.0F));
  EXPECT(near(layer.y, 10.0F));
}

// One axis each way, which is `repeat-x`: the tile spans the painting area
// downwards and steps by the image's width across.
TEST(background_one_axis_can_repeat_while_the_other_does_not) {
  const BackgroundLayer layer = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(20.0F, UnitType::Point, 20.0F, UnitType::Point),
      at(0.0F, UnitType::Point, 30.0F, UnitType::Point),
      repeating(BackgroundRepeatStyle::Repeat, BackgroundRepeatStyle::NoRepeat));

  EXPECT(layer.repeatsX);
  EXPECT(!layer.repeatsY);
  EXPECT(near(layer.tileWidth, 20.0F));
  EXPECT(near(layer.tileHeight, 100.0F));
  // Backed up to cover the border box: 10 - 20 is -10.
  EXPECT(near(layer.tileX, -10.0F));
  EXPECT(near(layer.tileY, 0.0F));
  // The row sits where the position put it, inside the tile.
  EXPECT(near(layer.y, 40.0F));
}

// `round` divides the area into a whole number of tiles before anything else
// happens, because it changes the size: 30pt into 80pt is 2.67 tiles, so the
// nearest whole number is 3 and the tile becomes 26.67.
TEST(background_round_divides_the_area_into_whole_tiles) {
  const BackgroundLayer layer = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(30.0F, UnitType::Point, 30.0F, UnitType::Point),
      BackgroundPosition{},
      repeating(BackgroundRepeatStyle::Round, BackgroundRepeatStyle::Round));

  EXPECT(near(layer.width, 80.0F / 3.0F));
  EXPECT(near(layer.height, 80.0F / 3.0F));
  // And it tiles by that size, with no gap: `round` is `repeat` with the size
  // adjusted.
  EXPECT(near(layer.tileWidth, layer.width));
  EXPECT(layer.repeatsX);

  // A size that already divides the area is left alone, which is the branch that
  // stops a division by a remainder of zero turning into a wrong size.
  const BackgroundLayer exact = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(40.0F, UnitType::Point, 40.0F, UnitType::Point),
      BackgroundPosition{},
      repeating(BackgroundRepeatStyle::Round, BackgroundRepeatStyle::Round));
  EXPECT(near(exact.width, 40.0F));
}

// `space` keeps the size and shares the slack between the tiles, and the first
// tile is flush with the area rather than where the position asked: 30pt into
// 80pt fits twice with 20pt left over, so the step is 50.
TEST(background_space_shares_the_slack_between_tiles) {
  const BackgroundLayer layer = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(30.0F, UnitType::Point, 30.0F, UnitType::Point),
      at(50.0F, UnitType::Percent, 50.0F, UnitType::Percent),
      repeating(BackgroundRepeatStyle::Space, BackgroundRepeatStyle::Space));

  EXPECT(near(layer.width, 30.0F));
  EXPECT(near(layer.tileWidth, 50.0F));
  EXPECT(layer.repeatsX);
  // Anchored on the area, not on the position: 10 backed up by one step is -40.
  EXPECT(near(layer.tileX, -40.0F));
  EXPECT(near(layer.x, layer.tileX));
}

// `space` with one tile has no slack to share, so it stops repeating -- which is
// the case that would otherwise divide by zero.
TEST(background_space_with_one_tile_does_not_repeat) {
  const BackgroundLayer layer = resolveBackgroundLayer(
      kPositioning,
      kPainting,
      sized(60.0F, UnitType::Point, 60.0F, UnitType::Point),
      BackgroundPosition{},
      repeating(BackgroundRepeatStyle::Space, BackgroundRepeatStyle::Space));

  EXPECT(!layer.repeatsX);
  EXPECT(!layer.repeatsY);
  EXPECT(near(layer.width, 60.0F));
  EXPECT(near(layer.x, 10.0F));
}

// Nothing to paint, which both hosts have to be told rather than left to divide
// by it: a view mid-layout has no size at all.
TEST(background_an_empty_area_is_nothing_to_paint) {
  const BackgroundArea nothing{0.0F, 0.0F, 0.0F, 0.0F};
  EXPECT(resolveBackgroundLayer(nothing, kPainting, autoSize(), BackgroundPosition{},
                                BackgroundRepeat{})
             .empty());
  EXPECT(resolveBackgroundLayer(kPositioning, nothing, autoSize(), BackgroundPosition{},
                                BackgroundRepeat{})
             .empty());
  // And a size of zero, which a stylesheet can ask for.
  EXPECT(resolveBackgroundLayer(kPositioning,
                                kPainting,
                                sized(0.0F, UnitType::Point, 10.0F, UnitType::Point),
                                BackgroundPosition{},
                                BackgroundRepeat{})
             .empty());
}
