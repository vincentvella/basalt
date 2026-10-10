// Which image-fetch ticks become `<Image onProgress>` events.
//
// The wiring -- libcurl's callback, the fetch thread, the event emitter -- is
// each host's and is proved end to end by the scenario that serves an image
// slowly over http. What is tested here is the decision: see
// core/ImageProgress.h.

#include "TestHarness.h"

#include "ImageProgress.h"

#include <sstream>

using basalt::ImageProgressTicker;

TEST(image_progress_reports_each_whole_percent) {
  ImageProgressTicker ticker;

  const auto first = ticker.tick(0, 1000);
  EXPECT(first.has_value());
  EXPECT_EQ((long)(*first * 1000), 0L);

  const auto tenth = ticker.tick(100, 1000);
  EXPECT(tenth.has_value());
  EXPECT_EQ((long)(*tenth * 1000), 100L);
  EXPECT_EQ((long)ticker.reportedPercent(), 10L);

  const auto whole = ticker.tick(1000, 1000);
  EXPECT(whole.has_value());
  EXPECT_EQ((long)(*whole * 1000), 1000L);
  EXPECT_EQ((long)ticker.reportedPercent(), 100L);
}

// The whole reason the gate exists. libcurl calls its progress function once
// per socket read, which for a large image is thousands of times; without this
// each one would be a Fabric event and a React render.
TEST(image_progress_says_nothing_twice) {
  ImageProgressTicker ticker;

  EXPECT(ticker.tick(500, 100000).has_value());
  // Five more bytes of a hundred thousand is the same percentage, and a bar
  // cannot draw the difference.
  EXPECT(!ticker.tick(501, 100000).has_value());
  EXPECT(!ticker.tick(502, 100000).has_value());
  EXPECT(!ticker.tick(999, 100000).has_value());
  // And the next percentage is.
  EXPECT(ticker.tick(1000, 100000).has_value());
}

// A chunked response has no Content-Length, which is not an error: the event
// carries the bytes and a zero total, and an app shows bytes rather than a
// fraction. Every tick, because there is no percentage to throttle on.
TEST(image_progress_an_unknown_total_reports_every_tick) {
  ImageProgressTicker ticker;

  const auto first = ticker.tick(1000, 0);
  EXPECT(first.has_value());
  EXPECT_EQ((long)(*first * 1000), 0L);
  EXPECT(ticker.tick(1001, 0).has_value());
  EXPECT(ticker.tick(1002, 0).has_value());
}

// libcurl's opening tick, before the response headers have been read. Nothing
// has arrived and nothing is known, so there is nothing to tell an app.
TEST(image_progress_the_opening_tick_is_not_an_event) {
  ImageProgressTicker ticker;

  EXPECT(!ticker.tick(0, 0).has_value());
  EXPECT_EQ((long)ticker.reportedPercent(), -1L);
  // And the first real tick still is one.
  EXPECT(ticker.tick(0, 1000).has_value());
}

// A server that sends more than it promised. `progress` is what an app
// multiplies its bar's width by, so it may not exceed one.
TEST(image_progress_never_passes_one) {
  ImageProgressTicker ticker;

  const auto over = ticker.tick(2000, 1000);
  EXPECT(over.has_value());
  EXPECT_EQ((long)(*over * 1000), 1000L);
  EXPECT_EQ((long)ticker.reportedPercent(), 100L);
}

// Negative counts, which libcurl has been seen to report for a transfer it has
// not started: not an event's worth of information, and certainly not a
// negative bar.
TEST(image_progress_a_negative_count_is_zero) {
  ImageProgressTicker ticker;

  const auto negative = ticker.tick(-5, 1000);
  EXPECT(negative.has_value());
  EXPECT_EQ((long)(*negative * 1000), 0L);
}
