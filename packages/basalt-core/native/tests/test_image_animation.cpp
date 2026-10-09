// core/ImageAnimation.h: the clamp and the frame arithmetic.
//
// Both hosts animate from an elapsed time rather than from a clock of their
// own, so this is where the awkward cases are asserted and each host's suite
// then asks only whether the right pixels arrived.

#include "TestHarness.h"

#include "ImageAnimation.h"

#include <sstream>
#include <vector>

namespace {

const std::vector<unsigned> kTwoFrames{100U, 100U};

} // namespace

TEST(image_animation_a_single_frame_is_finished_before_it_starts) {
  const auto step = basalt::imageAnimationStep({100U}, 0U, 0U);
  EXPECT_EQ(step.frame, std::size_t(0));
  EXPECT(step.finished);
  EXPECT_EQ(step.nextInMs, 0U);

  // And no frames at all, which is what a still image decodes to.
  const auto none = basalt::imageAnimationStep({}, 0U, 500U);
  EXPECT_EQ(none.frame, std::size_t(0));
  EXPECT(none.finished);
}

TEST(image_animation_the_frame_follows_the_elapsed_time) {
  const auto at = [](std::uint64_t elapsed) {
    return basalt::imageAnimationStep(kTwoFrames, 0U, elapsed);
  };

  EXPECT_EQ(at(0U).frame, std::size_t(0));
  EXPECT_EQ(at(99U).frame, std::size_t(0));
  EXPECT_EQ(at(100U).frame, std::size_t(1));
  EXPECT_EQ(at(199U).frame, std::size_t(1));
  // A loop count of zero is forever, so the third hundred milliseconds is the
  // first frame again.
  EXPECT_EQ(at(200U).frame, std::size_t(0));
  EXPECT(!at(200U).finished);
}

TEST(image_animation_says_when_the_next_frame_is_due) {
  // What a host sets its timer to. The whole delay at the start of a frame, and
  // what is left of it in the middle.
  EXPECT_EQ(basalt::imageAnimationStep(kTwoFrames, 0U, 0U).nextInMs, 100U);
  EXPECT_EQ(basalt::imageAnimationStep(kTwoFrames, 0U, 60U).nextInMs, 40U);
  EXPECT_EQ(basalt::imageAnimationStep(kTwoFrames, 0U, 199U).nextInMs, 1U);
}

TEST(image_animation_frames_have_their_own_delays) {
  // The common shape of a hand-drawn GIF: one long frame and several short
  // ones. A single delay for the whole file would put the boundaries in the
  // wrong places, which is invisible in a two-frame test where they are equal.
  const std::vector<unsigned> uneven{500U, 50U, 50U};

  const auto at = [&](std::uint64_t elapsed) {
    return basalt::imageAnimationStep(uneven, 0U, elapsed).frame;
  };
  EXPECT_EQ(at(0U), std::size_t(0));
  EXPECT_EQ(at(499U), std::size_t(0));
  EXPECT_EQ(at(500U), std::size_t(1));
  EXPECT_EQ(at(549U), std::size_t(1));
  EXPECT_EQ(at(550U), std::size_t(2));
  EXPECT_EQ(at(600U), std::size_t(0));
}

TEST(image_animation_a_delay_of_zero_becomes_a_tenth_of_a_second) {
  // The browsers' rule, and the reason a GIF written with no delay at all does
  // not spin at the display's refresh rate.
  EXPECT_EQ(basalt::clampedImageFrameDelay(0U), 100U);
  EXPECT_EQ(basalt::clampedImageFrameDelay(10U), 100U);
  EXPECT_EQ(basalt::clampedImageFrameDelay(11U), 11U);
  EXPECT_EQ(basalt::clampedImageFrameDelay(1000U), 1000U);

  // And through the arithmetic, where it decides the boundary: without the
  // clamp two zero-delay frames have no cycle to divide and the first frame
  // would be the only one ever shown.
  const std::vector<unsigned> instant{0U, 0U};
  EXPECT_EQ(basalt::imageAnimationStep(instant, 0U, 0U).frame, std::size_t(0));
  EXPECT_EQ(basalt::imageAnimationStep(instant, 0U, 100U).frame, std::size_t(1));
  EXPECT_EQ(basalt::imageAnimationStep(instant, 0U, 100U).nextInMs, 100U);
}

TEST(image_animation_a_loop_count_stops_on_the_last_frame) {
  const auto at = [](std::uint64_t elapsed) {
    return basalt::imageAnimationStep(kTwoFrames, 2U, elapsed);
  };

  EXPECT_EQ(at(0U).frame, std::size_t(0));
  EXPECT(!at(0U).finished);
  // Second pass, still running.
  EXPECT_EQ(at(300U).frame, std::size_t(1));
  EXPECT(!at(300U).finished);
  // Two passes of 200ms are over, so it stops, and on the frame the animation
  // was drawn to end on rather than back at the first.
  EXPECT_EQ(at(400U).frame, std::size_t(1));
  EXPECT(at(400U).finished);
  EXPECT_EQ(at(400U).nextInMs, 0U);
  EXPECT(at(100000U).finished);
}
