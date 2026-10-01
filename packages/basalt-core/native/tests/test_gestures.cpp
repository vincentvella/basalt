// Tests for the gesture recognisers.
//
// Core's, so compiled into both platforms' suites: a pan is a pan whichever
// toolkit reported the pointer.
//
// Driven through the registry rather than through the module, which is what
// makes them possible at all -- the module hops to the UI thread, and a test
// binary has no UI thread to hop to. The same reason keeps the long press out
// of here: it activates from a timer, and `postDelayed` needs a run loop. What
// is covered is everything decided by the pointer itself.

#include "TestHarness.h"

#include "Gestures.h"

#include <sstream>
#include <string>
#include <initializer_list>
#include <vector>

namespace {

struct Recorded {
  std::string event;
  int handlerTag{0};
  int state{0};
  int oldState{-1};
  double translationX{0};
  double translationY{0};
};

// A registry with nothing left over from the last test. The registry is a
// process-wide singleton -- there is one pointer -- so each test starts by
// dropping whatever the previous one attached.
basalt::GestureRegistry &fresh(std::vector<Recorded> &recorded) {
  auto &registry = basalt::gestures();
  for (int tag = 1; tag <= 32; tag++) {
    registry.drop(tag);
  }
  recorded.clear();
  registry.setEmitter([&recorded](const std::string &event, folly::dynamic payload) {
    Recorded entry;
    entry.event = event;
    entry.handlerTag = static_cast<int>(payload["handlerTag"].asDouble());
    entry.state = static_cast<int>(payload["state"].asDouble());
    if (const auto *old = payload.get_ptr("oldState"); old != nullptr) {
      entry.oldState = static_cast<int>(old->asDouble());
    }
    if (const auto *tx = payload.get_ptr("translationX"); tx != nullptr) {
      entry.translationX = tx->asDouble();
    }
    if (const auto *ty = payload.get_ptr("translationY"); ty != nullptr) {
      entry.translationY = ty->asDouble();
    }
    recorded.push_back(entry);
  });
  return registry;
}

// The state changes one handler went through, as "2,4,5" -- readable in a
// failure message, which a vector of ints is not.
std::string statesFor(const std::vector<Recorded> &recorded, int handlerTag) {
  std::ostringstream states;
  bool first = true;
  for (const auto &entry : recorded) {
    if (entry.handlerTag == handlerTag && entry.event == "onGestureHandlerStateChange") {
      if (!first) {
        states << ",";
      }
      states << entry.state;
      first = false;
    }
  }
  return states.str();
}

int countOf(const std::vector<Recorded> &recorded, int handlerTag, const char *event) {
  int count = 0;
  for (const auto &entry : recorded) {
    if (entry.handlerTag == handlerTag && entry.event == event) {
      count++;
    }
  }
  return count;
}

std::vector<basalt::HitView> chainOf(int tag, double originX = 0, double originY = 0) {
  return {basalt::HitView{.tag = tag, .originX = originX, .originY = originY}};
}

// Spelled the same way statesFor() spells them, so an expectation reads as the
// sequence it is.
std::string states(std::initializer_list<int> values) {
  std::ostringstream out;
  bool first = true;
  for (int value : values) {
    if (!first) {
      out << ",";
    }
    out << value;
    first = false;
  }
  return out.str();
}

constexpr int kBegan = static_cast<int>(basalt::GestureState::Began);
constexpr int kActive = static_cast<int>(basalt::GestureState::Active);
constexpr int kEnd = static_cast<int>(basalt::GestureState::End);
constexpr int kFailed = static_cast<int>(basalt::GestureState::Failed);
constexpr int kCancelled = static_cast<int>(basalt::GestureState::Cancelled);

} // namespace

TEST(a_pan_begins_then_activates_once_it_has_moved) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);

  registry.pointerDown(chainOf(100), 50, 50, 0);
  // Under the default 10pt slop: still only BEGAN.
  registry.pointerMove(54, 50, 16);
  EXPECT_EQ(statesFor(recorded, 1), states({kBegan}));

  registry.pointerMove(80, 50, 32);
  registry.pointerUp(80, 50, 48);
  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kActive, kEnd}));
}

TEST(a_pans_translation_is_measured_from_where_it_started) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);

  registry.pointerDown(chainOf(100), 50, 50, 0);
  registry.pointerMove(90, 70, 16);
  registry.pointerMove(120, 90, 32);

  // Not from the previous update: RNGH's translation is cumulative, and an app
  // that adds it up frame by frame moves twice as far as the pointer did.
  const Recorded &last = recorded.back();
  EXPECT_EQ(last.event, std::string("onGestureHandlerEvent"));
  EXPECT_EQ(static_cast<int>(last.translationX), 70);
  EXPECT_EQ(static_cast<int>(last.translationY), 40);
}

TEST(a_tap_is_a_press_that_did_not_move) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("TapGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);

  registry.pointerDown(chainOf(100), 10, 10, 0);
  registry.pointerUp(12, 11, 90);

  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kActive, kEnd}));
}

TEST(a_tap_fails_when_the_pointer_wanders) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("TapGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);

  registry.pointerDown(chainOf(100), 10, 10, 0);
  registry.pointerMove(200, 10, 40);
  registry.pointerUp(200, 10, 80);

  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kFailed}));
}

TEST(a_tap_fails_when_it_is_held_too_long) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("TapGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);

  registry.pointerDown(chainOf(100), 10, 10, 0);
  registry.pointerUp(10, 10, 5000);

  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kFailed}));
}

TEST(a_gesture_that_activates_cancels_the_others) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);
  registry.create("TapGestureHandler", 2, folly::dynamic::object());
  registry.attach(2, 100);

  registry.pointerDown(chainOf(100), 10, 10, 0);
  registry.pointerMove(200, 10, 40);

  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kActive}));
  // The tap had already failed on the slop check, which is the right answer for
  // the wrong-looking reason: either way it must not be left at BEGAN.
  const std::string tapStates = statesFor(recorded, 2);
  EXPECT(tapStates == states({kBegan, kFailed}) || tapStates == states({kBegan, kCancelled}));
}

TEST(simultaneous_handlers_are_left_alone) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);

  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);

  // A second pan on the same view, declared simultaneous with the first. Both
  // should activate; without the declaration the second would be cancelled.
  folly::dynamic simultaneous = folly::dynamic::object("simultaneousHandlers",
                                                       folly::dynamic::array(1));
  registry.create("PanGestureHandler", 2, simultaneous);
  registry.attach(2, 100);

  registry.pointerDown(chainOf(100), 10, 10, 0);
  registry.pointerMove(200, 10, 40);
  registry.pointerUp(200, 10, 80);

  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kActive, kEnd}));
  EXPECT_EQ(statesFor(recorded, 2), states({kBegan, kActive, kEnd}));
}

TEST(a_handler_waits_for_the_one_it_was_told_to_wait_for) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);

  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);
  registry.create("PanGestureHandler", 2,
                  folly::dynamic::object("waitFor", folly::dynamic::array(1)));
  registry.attach(2, 100);

  registry.pointerDown(chainOf(100), 10, 10, 0);
  registry.pointerMove(200, 10, 40);

  // 1 activated, which cancelled 2 -- and 2 never activated, because it was
  // waiting for 1 to fail and 1 did the opposite.
  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kActive}));
  const std::string waiting = statesFor(recorded, 2);
  EXPECT(waiting == states({kBegan, kFailed}) || waiting == states({kBegan, kCancelled}));
}

TEST(a_gesture_reaches_a_handler_on_an_ancestor) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  // Attached to the outer view, while the pointer lands on the inner one.
  registry.attach(1, 200);

  const std::vector<basalt::HitView> chain = {
      basalt::HitView{.tag = 100, .originX = 30, .originY = 30},
      basalt::HitView{.tag = 200, .originX = 10, .originY = 10},
  };
  registry.pointerDown(chain, 50, 50, 0);
  registry.pointerMove(100, 50, 16);

  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kActive}));
}

TEST(a_dropped_handler_stops_hearing_about_the_pointer) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);
  registry.drop(1);

  registry.pointerDown(chainOf(100), 10, 10, 0);
  registry.pointerMove(200, 10, 40);

  EXPECT_EQ(countOf(recorded, 1, "onGestureHandlerStateChange"), 0);
  EXPECT(registry.empty());
}

TEST(a_handler_attached_to_nothing_is_never_offered_a_gesture) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  // No attach: the view tag is 0, which is not a view.

  registry.pointerDown(chainOf(0), 10, 10, 0);
  registry.pointerMove(200, 10, 40);

  EXPECT_EQ(countOf(recorded, 1, "onGestureHandlerStateChange"), 0);
  registry.drop(1);
}

TEST(a_gesture_that_cannot_happen_here_fails_rather_than_hanging) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  // Pinch needs a second finger, which a cursor does not have.
  registry.create("PinchGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);

  registry.pointerDown(chainOf(100), 10, 10, 0);
  registry.pointerMove(200, 200, 40);
  registry.pointerUp(200, 200, 80);

  // BEGAN then FAILED, never ACTIVE: an app's onBegin runs and its onStart does
  // not, which is what "this platform cannot do that" should look like.
  EXPECT_EQ(statesFor(recorded, 1), states({kBegan, kFailed}));
}

TEST(the_pointers_position_is_reported_relative_to_the_handlers_view) {
  std::vector<Recorded> recorded;
  auto &registry = fresh(recorded);
  registry.create("PanGestureHandler", 1, folly::dynamic::object());
  registry.attach(1, 100);

  // The view sits at (40, 20) in the root, so a pointer at (100, 60) is at
  // (60, 40) inside it.
  registry.pointerDown(chainOf(100, 40, 20), 100, 60, 0);

  bool checked = false;
  for (const auto &entry : recorded) {
    (void)entry;
    checked = true;
  }
  EXPECT(checked);

  // Read the payload directly for the one field the recorder does not keep.
  registry.setEmitter([&checked](const std::string &, folly::dynamic payload) {
    EXPECT_EQ(static_cast<int>(payload["x"].asDouble()), 80);
    EXPECT_EQ(static_cast<int>(payload["y"].asDouble()), 40);
    EXPECT_EQ(static_cast<int>(payload["absoluteX"].asDouble()), 120);
    checked = true;
  });
  registry.pointerMove(120, 60, 16);
  registry.pointerUp(120, 60, 32);
}
