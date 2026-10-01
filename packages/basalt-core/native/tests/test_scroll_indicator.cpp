// Where a scrollbar's thumb goes. Arithmetic, so it can be asserted without a
// display -- which matters more here than usual, because a scrollbar is pure
// paint and paint is what these hosts cannot otherwise check.

#include "TestHarness.h"

#include "ScrollIndicator.h"

#include <sstream>

using basalt::kScrollIndicatorInset;
using basalt::kScrollIndicatorMinimumLength;
using basalt::ScrollIndicator;
using basalt::scrollIndicatorFor;

namespace {

TEST(indicator_content_that_fits_has_none) {
  // A scrollbar for content that cannot scroll is noise, and every list shorter
  // than its container would otherwise grow one.
  EXPECT(!scrollIndicatorFor(400, 400, 0).visible);
  EXPECT(!scrollIndicatorFor(400, 120, 0).visible);
}

TEST(indicator_a_long_list_gets_a_short_thumb) {
  // The thumb is the visible fraction: a quarter on screen, a quarter of the
  // track.
  const ScrollIndicator indicator = scrollIndicatorFor(400, 1600, 0);
  EXPECT(indicator.visible);
  const double track = 400 - 2 * kScrollIndicatorInset;
  EXPECT(indicator.length > track * 0.24);
  EXPECT(indicator.length < track * 0.26);
}

TEST(indicator_a_very_long_list_still_has_a_grabbable_thumb) {
  // Without a floor this is under a pixel, which stops reading as a position.
  const ScrollIndicator indicator = scrollIndicatorFor(400, 400000, 0);
  EXPECT_EQ(indicator.length, kScrollIndicatorMinimumLength);
}

TEST(indicator_at_the_top_it_sits_at_the_top) {
  const ScrollIndicator indicator = scrollIndicatorFor(400, 1600, 0);
  EXPECT_EQ(indicator.offset, kScrollIndicatorInset);
}

TEST(indicator_at_the_bottom_it_sits_at_the_bottom) {
  // The end of the track, not past it: the thumb's far edge lands on the inset.
  const ScrollIndicator indicator = scrollIndicatorFor(400, 1600, 1200);
  const double track = 400 - 2 * kScrollIndicatorInset;
  EXPECT_EQ(indicator.offset + indicator.length, kScrollIndicatorInset + track);
}

TEST(indicator_halfway_is_halfway) {
  const ScrollIndicator indicator = scrollIndicatorFor(400, 1600, 600);
  const double track = 400 - 2 * kScrollIndicatorInset;
  const double expected = kScrollIndicatorInset + 0.5 * (track - indicator.length);
  EXPECT_EQ(indicator.offset, expected);
}

TEST(indicator_an_overscroll_does_not_move_it_past_the_ends) {
  // An elastic overscroll goes negative at the top and past the end at the
  // bottom. A thumb that vanished there would flicker at exactly the moment
  // somebody is looking at it.
  const ScrollIndicator above = scrollIndicatorFor(400, 1600, -80);
  EXPECT(above.visible);
  EXPECT_EQ(above.offset, kScrollIndicatorInset);

  const ScrollIndicator below = scrollIndicatorFor(400, 1600, 1600);
  const double track = 400 - 2 * kScrollIndicatorInset;
  EXPECT_EQ(below.offset + below.length, kScrollIndicatorInset + track);
}

TEST(indicator_a_container_of_nothing_has_none) {
  // A view that has not been laid out yet, which happens on the first mount.
  EXPECT(!scrollIndicatorFor(0, 1600, 0).visible);
}

} // namespace

// --------------------------------------------------------------------------
// Insets
// --------------------------------------------------------------------------
//
// The two kinds do different jobs, and the tests are separate for that reason:
// `contentInset` changes how far there is to go, and `scrollIndicatorInsets`
// changes only where the bar is drawn.

TEST(indicator_a_content_inset_leaves_room_at_the_top) {
  // 100 of container, 400 of content, and 50 more to pull into above it. At the
  // very top the offset is -50, and the thumb belongs at the start of the track.
  const auto top =
      basalt::scrollIndicatorFor(100.0, 400.0, -50.0, basalt::ScrollAxisInsets{50.0, 0.0});
  EXPECT(top.visible);
  EXPECT_NEAR(top.offset, basalt::kScrollIndicatorInset, 0.001);

  // And at the bottom it still ends at the end.
  const auto bottom =
      basalt::scrollIndicatorFor(100.0, 400.0, 300.0, basalt::ScrollAxisInsets{50.0, 0.0});
  EXPECT_NEAR(bottom.offset + bottom.length, 100.0 - basalt::kScrollIndicatorInset, 0.001);
}

// Offset zero is the top of the *content*, which with a leading inset is no
// longer the top of the range -- so the thumb is a little way down. This is the
// case that was wrong before insets were read at all.
TEST(indicator_a_content_inset_moves_where_zero_sits) {
  const auto without = basalt::scrollIndicatorFor(100.0, 400.0, 0.0);
  const auto with =
      basalt::scrollIndicatorFor(100.0, 400.0, 0.0, basalt::ScrollAxisInsets{50.0, 0.0});
  EXPECT(with.offset > without.offset);
}

TEST(indicator_an_indicator_inset_shortens_the_track) {
  // A 400 view over 800 of content, so the thumb is about half the track and
  // well clear of the minimum length -- which at a 100 view would have been
  // the answer either way, and would have made this pass without proving
  // anything.
  const auto plain = basalt::scrollIndicatorFor(400.0, 800.0, 0.0);
  const auto inset = basalt::scrollIndicatorFor(
      400.0, 800.0, 0.0, basalt::ScrollAxisInsets{}, basalt::ScrollAxisInsets{20.0, 10.0});

  // It starts below the inset...
  EXPECT_NEAR(inset.offset, basalt::kScrollIndicatorInset + 20.0, 0.001);
  // ...and a shorter track means a shorter thumb.
  EXPECT(inset.length < plain.length);

  // At the far end it stops short of the trailing inset.
  const auto end = basalt::scrollIndicatorFor(
      400.0, 800.0, 400.0, basalt::ScrollAxisInsets{}, basalt::ScrollAxisInsets{20.0, 10.0});
  EXPECT_NEAR(end.offset + end.length, 400.0 - basalt::kScrollIndicatorInset - 10.0, 0.001);
}

// An indicator inset does not change what there is to scroll through, so it
// must not change the *fraction* the thumb represents beyond the track being
// shorter -- and it must not make the bar disappear.
TEST(indicator_an_indicator_inset_alone_does_not_hide_the_bar) {
  const auto indicator = basalt::scrollIndicatorFor(
      100.0, 400.0, 0.0, basalt::ScrollAxisInsets{}, basalt::ScrollAxisInsets{40.0, 40.0});
  EXPECT(indicator.visible);
  EXPECT(indicator.length > 0.0);
}

// Content that fits exactly, with room to pull above it: there *is* somewhere
// to go, so there is something to indicate. Asking the content rather than the
// range would have called this unscrollable.
TEST(indicator_content_that_fits_but_can_be_pulled_still_has_a_bar) {
  const auto indicator =
      basalt::scrollIndicatorFor(100.0, 100.0, 0.0, basalt::ScrollAxisInsets{50.0, 0.0});
  EXPECT(indicator.visible);
}
