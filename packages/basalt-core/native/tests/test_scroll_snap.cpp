// Where a scroll comes to rest: paging, intervals and explicit offsets.
//
// Arithmetic, so it can be asserted without a display -- which is most of the
// reason it lives in core rather than in three hosts.

#include "TestHarness.h"

#include "ScrollSnap.h"

#include <sstream>

using basalt::ScrollSnapAlignment;
using basalt::ScrollSnapConfig;
using basalt::scrollSnapTarget;

namespace {

// A page-sized container over five pages of content.
constexpr double kContainer = 400.0;
constexpr double kContent = 2000.0;

double snapped(const ScrollSnapConfig &config, double offset, double velocity) {
  const auto target = scrollSnapTarget(config, offset, velocity, kContainer, kContent);
  return target ? *target : -1.0;
}

TEST(snap_nothing_configured_snaps_nothing) {
  // A plain ScrollView must be unaffected: this runs at the end of every drag.
  const ScrollSnapConfig none;
  EXPECT(!scrollSnapTarget(none, 137, 0, kContainer, kContent).has_value());
}

TEST(snap_paging_settles_on_the_nearest_page) {
  ScrollSnapConfig config;
  config.paging = true;
  // Just past a page boundary, released: back to the page behind.
  EXPECT_EQ(snapped(config, 420, 0), 400.0);
  // Most of the way to the next: forward to it.
  EXPECT_EQ(snapped(config, 790, 0), 800.0);
}

TEST(snap_a_flick_moves_a_whole_page) {
  // The rule that makes a carousel feel like one: a flick carries even when the
  // finger left nearer the page it started on.
  ScrollSnapConfig config;
  config.paging = true;
  EXPECT_EQ(snapped(config, 410, 900), 800.0);
}

TEST(snap_a_flick_backwards_moves_back) {
  ScrollSnapConfig config;
  config.paging = true;
  EXPECT_EQ(snapped(config, 790, -900), 400.0);
}

TEST(snap_an_interval_is_not_the_container) {
  ScrollSnapConfig config;
  config.interval = 250;
  EXPECT_EQ(snapped(config, 260, 0), 250.0);
  EXPECT_EQ(snapped(config, 380, 0), 500.0);
}

TEST(snap_explicit_offsets_win_over_an_interval) {
  // A list of points is a more specific statement than a spacing.
  ScrollSnapConfig config;
  config.interval = 250;
  config.offsets = {0, 90, 610};
  EXPECT_EQ(snapped(config, 100, 0), 90.0);
  EXPECT_EQ(snapped(config, 500, 0), 610.0);
}

TEST(snap_a_flick_across_explicit_offsets_moves_one_entry) {
  ScrollSnapConfig config;
  config.offsets = {0, 90, 610};
  EXPECT_EQ(snapped(config, 95, 900), 610.0);
  EXPECT_EQ(snapped(config, 600, -900), 90.0);
}

TEST(snap_never_past_the_end_of_the_content) {
  // The furthest a scroll view can be is content minus container. A flick at
  // the last page must not propose an offset beyond it, which would be a view
  // showing nothing.
  ScrollSnapConfig config;
  config.paging = true;
  EXPECT_EQ(snapped(config, 1600, 2000), kContent - kContainer);
}

TEST(snap_never_before_the_start) {
  ScrollSnapConfig config;
  config.paging = true;
  EXPECT_EQ(snapped(config, 10, -2000), 0.0);
}

TEST(snap_centre_alignment_lines_up_the_middle) {
  // With a 400 container and a 400 interval, centre alignment puts a boundary
  // at the middle of the view -- so the resting offsets sit half a container
  // back from the start-aligned ones.
  ScrollSnapConfig config;
  config.interval = 400;
  config.alignment = ScrollSnapAlignment::Center;
  EXPECT_EQ(snapped(config, 210, 0), 200.0);
}

// --- disableIntervalMomentum, and the momentum it disables -----------------
//
// A spacing is not a page: upstream lets the fling carry across several points
// and settles at the one it arrives near, which is what `snapToInterval` means
// on iOS. The prop asks for the paged behaviour instead.
//
// The projection is `core/ScrollMomentum.h`'s, so these numbers are the
// friction model's rather than a target: 2000px/s at the normal rate is about
// 984 pixels, which puts the next 250-point boundary at 1000.

TEST(snap_a_flick_carries_as_far_as_it_was_thrown) {
  ScrollSnapConfig config;
  config.interval = 250;
  EXPECT_EQ(snapped(config, 0, 2000), 1000.0);
  // And a gentler one carries less, which is the rule the header states and
  // the reason the projection is velocity-dependent rather than a page.
  EXPECT_EQ(snapped(config, 0, 400), 250.0);
}

TEST(snap_momentum_across_an_interval_can_be_disabled) {
  ScrollSnapConfig config;
  config.interval = 250;
  config.disableIntervalMomentum = true;
  // The next point from where the finger left, however hard the throw.
  EXPECT_EQ(snapped(config, 0, 2000), 250.0);
  EXPECT_EQ(snapped(config, 0, 400), 250.0);
}

TEST(snap_paging_turns_one_page_however_hard_it_is_thrown) {
  // Not a regression test for the projection so much as a statement that it
  // does not apply here: a paged view turns one page per flick on iOS, and a
  // throw worth two and a half pages still turns one.
  ScrollSnapConfig config;
  config.paging = true;
  EXPECT_EQ(snapped(config, 0, 4000), kContainer);
}

// --- snapToStart and snapToEnd ---------------------------------------------
//
// Both default to true, and what they are the default *of* is that the
// content's own edges count as snap points beside the listed ones. Which is
// easy to miss: a list of offsets that does not include 0 could not be scrolled
// back to the top and held there.

TEST(snap_the_content_edges_are_snap_points_by_default) {
  ScrollSnapConfig config;
  config.offsets = {300, 600};

  // Nearer the top than the first listed point, released: the top.
  EXPECT_EQ(snapped(config, 100, 0), 0.0);
  // And nearer the end than the last listed point: the end, which is the
  // content minus the container.
  EXPECT_EQ(snapped(config, 1500, 0), kContent - kContainer);
}

TEST(snap_to_start_false_scrolls_freely_before_the_first_offset) {
  ScrollSnapConfig config;
  config.offsets = {300, 600};
  config.snapToStart = false;
  // No answer at all, which is what lets the list coast: each host treats a
  // target of nothing as "not a snapping gesture" and runs the fling.
  EXPECT(!scrollSnapTarget(config, 100, 0, kContainer, kContent).has_value());
  // The listed points still snap, so this is the gap being freed rather than
  // snapping being switched off.
  EXPECT_EQ(snapped(config, 320, 0), 300.0);
}

TEST(snap_to_end_false_scrolls_freely_past_the_last_offset) {
  ScrollSnapConfig config;
  config.offsets = {300, 600};
  config.snapToEnd = false;
  EXPECT(!scrollSnapTarget(config, 1500, 0, kContainer, kContent).has_value());
  // The listed points still snap: it is the gap past the last of them that was
  // freed, not snapping itself. A list at 620 is in that gap and coasts, which
  // is what the assertion above says; one at 320 is not.
  EXPECT_EQ(snapped(config, 320, 0), 300.0);
}

TEST(snap_a_freed_edge_is_still_reached_from_before_it) {
  // The half of upstream's rule that is easy to drop: a list thrown from
  // *inside* the snapping region towards a freed edge settles on the last
  // listed point rather than sailing into the gap. Only a list already in the
  // gap coasts.
  ScrollSnapConfig config;
  config.offsets = {300, 600};
  config.snapToEnd = false;
  EXPECT_EQ(snapped(config, 500, 2000), 600.0);

  ScrollSnapConfig fromTheOtherEnd;
  fromTheOtherEnd.offsets = {300, 600};
  fromTheOtherEnd.snapToStart = false;
  EXPECT_EQ(snapped(fromTheOtherEnd, 500, -2000), 300.0);
}

TEST(snap_content_shorter_than_the_container_stays_at_zero) {
  ScrollSnapConfig config;
  config.paging = true;
  const auto target = scrollSnapTarget(config, 0, 900, 400, 200);
  EXPECT(target.has_value());
  EXPECT_EQ(*target, 0.0);
}

} // namespace
