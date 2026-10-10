// Keeping the content still while items are inserted above it.
//
// Arithmetic, so it can be asserted without a display and without a mounting
// transaction -- which is most of the reason it lives in core rather than in
// three hosts. What each host adds is the children's boxes.

#include "TestHarness.h"

#include "ScrollVisiblePosition.h"

#include <sstream>
#include <vector>

using basalt::firstVisibleChild;
using basalt::ScrollChildBox;
using basalt::visiblePositionAdjustment;

namespace {

// Ten rows of a hundred points each, tagged 100 upwards.
std::vector<ScrollChildBox> rows(int count = 10, double height = 100.0) {
  std::vector<ScrollChildBox> boxes;
  for (int index = 0; index < count; index++) {
    boxes.push_back(ScrollChildBox{.tag = 100 + index,
                                   .leading = static_cast<double>(index) * height,
                                   .length = height});
  }
  return boxes;
}

} // namespace

TEST(visible_the_first_visible_child_is_the_one_watched) {
  // Scrolled 250 down: the first two rows are above the viewport entirely and
  // the third is the first with anything on screen.
  const auto pinned = firstVisibleChild(rows(), 0, 250.0);
  EXPECT(pinned.has);
  EXPECT_EQ(pinned.tag, 102);
  EXPECT_NEAR(pinned.leading, 200.0, 0.0001);
}

TEST(visible_a_partly_visible_child_counts_as_visible) {
  // 199 down: one point of the second row is on screen, which is enough -- the
  // rule is the trailing edge being past the offset, not the leading edge.
  const auto pinned = firstVisibleChild(rows(), 0, 199.0);
  EXPECT(pinned.has);
  EXPECT_EQ(pinned.tag, 101);
}

TEST(visible_min_index_skips_the_children_before_it) {
  // What `minIndexForVisible` is for: a header that is not part of the list's
  // content and must not be the thing held still.
  const auto pinned = firstVisibleChild(rows(), 3, 0.0);
  EXPECT(pinned.has);
  EXPECT_EQ(pinned.tag, 103);
}

TEST(visible_the_last_child_is_the_fallback) {
  // Scrolled past everything: no child is visible, and the last one is watched
  // anyway. Without this a list at its end would adjust by nothing, which is
  // the case a chat view is in almost all the time.
  const auto pinned = firstVisibleChild(rows(), 0, 5000.0);
  EXPECT(pinned.has);
  EXPECT_EQ(pinned.tag, 109);
}

TEST(visible_an_empty_list_has_nothing_to_watch) {
  EXPECT(!firstVisibleChild({}, 0, 0.0).has);
  // And an index past the end is the app saying none of these: watching the
  // last child there would be watching one it excluded.
  EXPECT(!firstVisibleChild(rows(3), 5, 0.0).has);
}

TEST(visible_a_child_that_moved_down_moves_the_offset_with_it) {
  // A row inserted above the watched one: it is now 100 further down, so the
  // offset goes 100 further down and the content under the eye does not move.
  const auto adjustment = visiblePositionAdjustment(200.0, 300.0, 250.0, std::nullopt);
  EXPECT(adjustment.move);
  EXPECT_NEAR(adjustment.delta, 100.0, 0.0001);
  EXPECT(!adjustment.autoscrollToStart);
}

TEST(visible_a_child_that_moved_up_moves_the_offset_up) {
  // The other direction, which is a row above being removed.
  const auto adjustment = visiblePositionAdjustment(300.0, 200.0, 350.0, std::nullopt);
  EXPECT(adjustment.move);
  EXPECT_NEAR(adjustment.delta, -100.0, 0.0001);
}

TEST(visible_half_a_point_is_not_a_move) {
  // Layout repeats to within a rounding error, and a list that moved by that on
  // every transaction would drift under the person reading it.
  EXPECT(!visiblePositionAdjustment(200.0, 200.4, 250.0, std::nullopt).move);
  EXPECT(!visiblePositionAdjustment(200.0, 199.6, 250.0, std::nullopt).move);
  EXPECT(visiblePositionAdjustment(200.0, 200.6, 250.0, std::nullopt).move);
}

TEST(visible_a_list_near_the_start_follows_the_new_content) {
  // `autoscrollToTopThreshold`, which reads backwards until the case is named:
  // a chat list sitting at the top, where the new messages arrive, should
  // follow them rather than hold its place.
  const auto adjustment = visiblePositionAdjustment(200.0, 300.0, 40.0, 50);
  EXPECT(adjustment.move);
  EXPECT(adjustment.autoscrollToStart);
}

TEST(visible_a_list_away_from_the_start_holds_its_place) {
  // The same insertion with the list further down: it holds still, which is the
  // behaviour the prop is mostly about.
  const auto adjustment = visiblePositionAdjustment(200.0, 300.0, 400.0, 50);
  EXPECT(adjustment.move);
  EXPECT(!adjustment.autoscrollToStart);
}

TEST(visible_no_threshold_never_autoscrolls) {
  // The prop's other half is optional, and an absent one must not be read as
  // zero: a list exactly at the start would then be dragged there every time.
  const auto adjustment = visiblePositionAdjustment(200.0, 300.0, 0.0, std::nullopt);
  EXPECT(adjustment.move);
  EXPECT(!adjustment.autoscrollToStart);
}
