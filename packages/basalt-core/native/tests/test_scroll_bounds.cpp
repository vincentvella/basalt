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

// --- centerContent -----------------------------------------------------------
//
// An inset rather than a position, because the range above is already the thing
// that decides where content rests: half the slack at each end leaves exactly
// one offset to be at, and the clamp every host runs on every mutation puts the
// content there. iOS reaches it the same way, assigning a `contentInset`.

TEST(bounds_centering_insets_are_half_the_slack_at_each_end) {
  const basalt::ScrollAxisInsets insets = basalt::centeringInsets(300, 200);
  EXPECT_NEAR(insets.leading, 50.0, 0.0001);
  EXPECT_NEAR(insets.trailing, 50.0, 0.0001);
}

TEST(bounds_centering_leaves_one_offset_to_rest_at) {
  // The whole point: a range of no length, in the middle. So a view that was at
  // zero is pulled to -50, which draws the content half the slack further down.
  const basalt::ScrollRange range =
      basalt::scrollRangeFor(300, 200, basalt::centeringInsets(300, 200));
  EXPECT_NEAR(range.minimum, -50.0, 0.0001);
  EXPECT_NEAR(range.maximum, -50.0, 0.0001);
  EXPECT_NEAR(basalt::clampScrollOffset(0, 300, 200, basalt::centeringInsets(300, 200)),
              -50.0,
              0.0001);
}

TEST(bounds_centering_does_nothing_to_content_that_can_scroll) {
  // Content longer than its container has no slack to share out, and an inset
  // here would be a list that could be pulled past its own first row.
  const basalt::ScrollAxisInsets insets = basalt::centeringInsets(300, 4000);
  EXPECT_NEAR(insets.leading, 0.0, 0.0001);
  EXPECT_NEAR(insets.trailing, 0.0, 0.0001);
  EXPECT_NEAR(basalt::clampScrollOffset(0, 300, 4000, insets), 0.0, 0.0001);
}

TEST(bounds_centering_content_exactly_the_container_is_not_centred) {
  // The boundary, and the one that would divide by nothing useful: there is no
  // slack, so there is no inset and the view rests at zero.
  const basalt::ScrollAxisInsets insets = basalt::centeringInsets(300, 300);
  EXPECT_NEAR(insets.leading, 0.0, 0.0001);
  EXPECT_NEAR(insets.trailing, 0.0, 0.0001);
}
