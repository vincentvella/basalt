// The door to the JavaScript thread that a host leaves open.
//
// What is testable here is the decision -- is there a runner, does work reach
// it, does clearing it stop the work -- and not the running, which needs a
// runtime and a host. So the runner in these tests records what it was handed
// rather than calling it: the point of the seam is that native code on some
// other thread can post *without* knowing whether anyone is listening, and that
// is exactly the part a test can pin down.
//
// The half that needs a runtime -- an event arriving at a JavaScript listener --
// is proven by the harness scenario, the same division test_blobs.cpp makes.

#include "TestHarness.h"

#include "ExpoModules.h"
#include "JsRuntimeAccess.h"

#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

// Restores the seam whatever a test does to it. Process-wide state that a later
// test reads, exactly like the blob registry.
struct RunnerFixture {
  ~RunnerFixture() { basalt::setRuntimeRunner(nullptr); }
};

} // namespace

TEST(without_a_host_there_is_nowhere_to_post) {
  // The tests and the portability probe, and the answer has to be a reported
  // false rather than a crash: a subprocess reader thread emitting into a
  // process that never had a runtime is ordinary, not exceptional.
  RunnerFixture fixture;
  basalt::setRuntimeRunner(nullptr);

  bool ran = false;
  EXPECT(!basalt::runOnJsRuntime([&ran](facebook::jsi::Runtime &) { ran = true; }));
  EXPECT(!ran);
}

TEST(work_reaches_the_runner_a_host_left) {
  RunnerFixture fixture;
  int posted = 0;
  basalt::setRuntimeRunner([&posted](std::function<void(facebook::jsi::Runtime &)>) {
    // Held rather than called: calling it needs a runtime. What is asserted is
    // that it arrived.
    posted++;
  });

  EXPECT(basalt::runOnJsRuntime([](facebook::jsi::Runtime &) {}));
  EXPECT(basalt::runOnJsRuntime([](facebook::jsi::Runtime &) {}));
  EXPECT_EQ(posted, 2);
}

TEST(clearing_the_runner_stops_the_posting) {
  // The shutdown path: a host clears this before it destroys its ReactHost, so
  // that work in flight from another thread does not reach a runtime that is
  // being joined.
  RunnerFixture fixture;
  int posted = 0;
  basalt::setRuntimeRunner([&posted](std::function<void(facebook::jsi::Runtime &)>) { posted++; });
  EXPECT(basalt::runOnJsRuntime([](facebook::jsi::Runtime &) {}));

  basalt::setRuntimeRunner(nullptr);
  EXPECT(!basalt::runOnJsRuntime([](facebook::jsi::Runtime &) {}));
  EXPECT_EQ(posted, 1);
}

TEST(an_expo_event_goes_through_the_same_door) {
  // Which is the whole reason the door exists: an Expo module is handed no call
  // invoker, so this is the only way it can emit.
  RunnerFixture fixture;
  basalt::setRuntimeRunner(nullptr);
  EXPECT(!basalt::emitExpoEvent("KinoProcess", "onOutput", [](facebook::jsi::Runtime &,
                                                             std::vector<facebook::jsi::Value> &) {
  }));

  int posted = 0;
  basalt::setRuntimeRunner([&posted](std::function<void(facebook::jsi::Runtime &)>) { posted++; });
  EXPECT(basalt::emitExpoEvent("KinoProcess", "onOutput", [](facebook::jsi::Runtime &,
                                                            std::vector<facebook::jsi::Value> &) {
  }));
  EXPECT_EQ(posted, 1);
}

TEST(the_call_invoker_over_the_runner_posts_and_says_it_cannot_wait) {
  RunnerFixture fixture;
  int posted = 0;
  basalt::setRuntimeRunner([&posted](std::function<void(facebook::jsi::Runtime &)>) { posted++; });

  const auto invoker = basalt::jsCallInvoker();
  EXPECT(invoker != nullptr);
  invoker->invokeAsync(facebook::react::CallFunc([](facebook::jsi::Runtime &) {}));
  EXPECT_EQ(posted, 1);

  // Rather than deadlocking, which is what a synchronous call from another
  // thread into a busy runtime would do; see the header.
  bool threw = false;
  try {
    invoker->invokeSync(facebook::react::CallFunc([](facebook::jsi::Runtime &) {}));
  } catch (const std::logic_error &error) {
    threw = true;
    // Self-naming, so the message alone says which invoker and why.
    EXPECT(std::string(error.what()).find("JsRuntimeAccess") != std::string::npos);
  }
  EXPECT(threw);
  EXPECT_EQ(posted, 1);
}

TEST(the_same_invoker_survives_a_reload) {
  // It holds nothing but the call to runOnJsRuntime, so a module that captured
  // it before a Fast Refresh reload is still holding something that works --
  // which is why one shared instance is safe to hand out.
  EXPECT(basalt::jsCallInvoker() == basalt::jsCallInvoker());
}
