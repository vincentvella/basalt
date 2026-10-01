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

TEST(snap_content_shorter_than_the_container_stays_at_zero) {
  ScrollSnapConfig config;
  config.paging = true;
  const auto target = scrollSnapTarget(config, 0, 900, 400, 200);
  EXPECT(target.has_value());
  EXPECT_EQ(*target, 0.0);
}

} // namespace
