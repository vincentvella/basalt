// The scrollable range, and what `contentInset` does to it.
//
// The arithmetic is four lines and was duplicated in three hosts, none of which
// read the insets. These are the cases that tell the old behaviour from the new
// one -- and the first three are the old behaviour, which must not move.

#include "TestHarness.h"

#include "ScrollBounds.h"

// EXPECT_NEAR builds its message with a stream; the harness's header does not
// pull one in, so each suite brings its own.
#include <sstream>

using basalt::clampScrollOffset;
using basalt::ScrollAxisInsets;
using basalt::scrollRangeFor;

TEST(bounds_without_insets_are_what_they_always_were) {
  const auto range = scrollRangeFor(100.0, 400.0);
  EXPECT_NEAR(range.minimum, 0.0, 0.001);
  EXPECT_NEAR(range.maximum, 300.0, 0.001);
}

TEST(bounds_content_that_fits_cannot_scroll) {
  const auto range = scrollRangeFor(400.0, 100.0);
  EXPECT_NEAR(range.minimum, 0.0, 0.001);
  EXPECT_NEAR(range.maximum, 0.0, 0.001);
}

TEST(bounds_clamp_holds_an_offset_inside) {
  EXPECT_NEAR(clampScrollOffset(-50.0, 100.0, 400.0), 0.0, 0.001);
  EXPECT_NEAR(clampScrollOffset(999.0, 100.0, 400.0), 300.0, 0.001);
  EXPECT_NEAR(clampScrollOffset(120.0, 100.0, 400.0), 120.0, 0.001);
}

TEST(bounds_a_leading_inset_allows_pulling_before_the_content) {
  const auto range = scrollRangeFor(100.0, 400.0, ScrollAxisInsets{50.0, 0.0});
  EXPECT_NEAR(range.minimum, -50.0, 0.001);
  // The far end is unchanged: a top inset does not add room at the bottom.
  EXPECT_NEAR(range.maximum, 300.0, 0.001);
  EXPECT_NEAR(clampScrollOffset(-50.0, 100.0, 400.0, ScrollAxisInsets{50.0, 0.0}), -50.0, 0.001);
  EXPECT_NEAR(clampScrollOffset(-80.0, 100.0, 400.0, ScrollAxisInsets{50.0, 0.0}), -50.0, 0.001);
}

TEST(bounds_a_trailing_inset_adds_room_at_the_far_end) {
  const auto range = scrollRangeFor(100.0, 400.0, ScrollAxisInsets{0.0, 40.0});
  EXPECT_NEAR(range.minimum, 0.0, 0.001);
  EXPECT_NEAR(range.maximum, 340.0, 0.001);
}

// Content shorter than its container, with a leading inset: there is somewhere
// to pull to and nowhere to scroll to, and the two ends must not cross.
TEST(bounds_an_inset_on_content_that_fits_does_not_invert_the_range) {
  const auto range = scrollRangeFor(400.0, 100.0, ScrollAxisInsets{50.0, 0.0});
  EXPECT_NEAR(range.minimum, -50.0, 0.001);
  EXPECT_NEAR(range.maximum, -50.0, 0.001);
  EXPECT_NEAR(clampScrollOffset(10.0, 400.0, 100.0, ScrollAxisInsets{50.0, 0.0}), -50.0, 0.001);
}

TEST(bounds_both_insets_extend_both_ends) {
  const auto range = scrollRangeFor(100.0, 400.0, ScrollAxisInsets{20.0, 30.0});
  EXPECT_NEAR(range.minimum, -20.0, 0.001);
  EXPECT_NEAR(range.maximum, 330.0, 0.001);
  EXPECT_NEAR(range.length(), 350.0, 0.001);
}
