// requestAnimationFrame's queue and the frame clock it shares.
//
// What a runtime adds on top -- installing the globals, calling the batch -- is
// covered end to end. What is asserted here is what decides whether an app
// spins or idles: which callbacks a frame runs, and when the clock is asked to
// run at all.

#include "TestHarness.h"

#include "AnimationFrames.h"
#include "DisplayChoreographer.h"

#include <string>
#include <vector>

using basalt::DisplayChoreographer;
using basalt::FrameQueue;

namespace {

// A host with a clock that only counts what it was told.
class FakeHost final : public DisplayChoreographer {
 public:
  int changes = 0;
  void tick() {
    deliverFrame(facebook::react::AnimationTimestamp{0});
  }

 protected:
  void runningChanged() override {
    ++changes;
  }
};

} // namespace

TEST(a_frame_runs_what_was_requested_in_order_and_nothing_cancelled) {
  FrameQueue<std::string> queue;
  const int a = queue.request("a");
  const int b = queue.request("b");
  queue.request("c");
  EXPECT(a != 0 && a != b);
  queue.cancel(b);
  queue.cancel(12345); // never requested: fine
  const auto batch = queue.take();
  EXPECT_EQ(batch.size(), size_t{2});
  EXPECT_EQ(batch[0], std::string("a"));
  EXPECT_EQ(batch[1], std::string("c"));
  EXPECT(queue.empty());
}

TEST(a_callback_requested_during_a_frame_waits_for_the_next) {
  FrameQueue<std::string> queue;
  queue.request("first");
  auto batch = queue.take();
  // What a running callback does: ask again.
  queue.request("again");
  EXPECT_EQ(batch.size(), size_t{1});
  EXPECT(!queue.empty());
  EXPECT_EQ(queue.take()[0], std::string("again"));
}

TEST(the_clock_runs_while_either_half_wants_frames_and_stops_when_neither_does) {
  FakeHost host;
  EXPECT(!host.running());
  host.setFramesWanted(true);
  EXPECT(host.running());
  host.resume(); // already running: no second change
  host.setFramesWanted(false);
  EXPECT(host.running()); // the backend still wants frames
  host.pause();
  EXPECT(!host.running());
  EXPECT_EQ(host.changes, 2);
}

TEST(a_tick_reaches_requestAnimationFrame_only_while_it_wants_frames) {
  FakeHost host;
  int frames = 0;
  host.setFrameListener([&frames] { ++frames; });
  host.resume(); // the backend's frames alone
  host.tick();
  EXPECT_EQ(frames, 0);
  host.setFramesWanted(true);
  host.tick();
  host.tick();
  EXPECT_EQ(frames, 2);
}
