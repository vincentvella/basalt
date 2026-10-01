// Tests for the hover gate.
//
// Core's, so compiled into every platform's suite: which views a cursor is
// inside is the toolkit's answer, and what to do about it is not.
//
// What is asserted here is small on purpose, because the hover *model* is
// React Native's own -- `PointerEventsProcessor` turns a stream of moves into
// enter, leave, over and out, and a host that also emits those gets every one
// of them twice over. See the header of HoverTracker.h. What is left to this
// project is deciding which moves are worth dispatching at all, and the case
// worth testing is the one that is easy to get wrong: the move *after* the
// cursor leaves everything that was listening.

#include "TestHarness.h"

#include "HoverTracker.h"

#include <cstdint>
#include <sstream>

namespace {

using basalt::HoverTracker;

constexpr std::uint16_t kListening = basalt::HoverListenerEnter | basalt::HoverListenerLeave;
constexpr std::uint16_t kSilent = basalt::HoverListenerNone;

} // namespace

TEST(hover_dispatches_a_move_over_a_view_that_listens) {
  HoverTracker tracker;
  EXPECT(tracker.admitMove(10, kListening));
  // Every move while the cursor is over it, because React Native works out
  // whether anything actually changed.
  EXPECT(tracker.admitMove(10, kListening));
  EXPECT(tracker.admitMove(11, kListening));
}

TEST(hover_dispatches_nothing_over_views_that_do_not) {
  HoverTracker tracker;
  // The reason the gate exists: a cursor crossing an app that uses no hover
  // must not cost a trip into JavaScript per motion event.
  EXPECT(!tracker.admitMove(10, kSilent));
  EXPECT(!tracker.admitMove(11, kSilent));
}

TEST(hover_dispatches_one_last_move_after_leaving_a_listening_view) {
  HoverTracker tracker;
  EXPECT(tracker.admitMove(10, kListening));
  // The cursor is now over something that listens to nothing. Suppressing this
  // move would leave React Native holding a hover path it never hears is
  // stale, and view 10 hovered for good -- so exactly one more goes through.
  EXPECT(tracker.admitMove(11, kSilent));
  EXPECT(!tracker.admitMove(12, kSilent));
}

TEST(hover_leaving_the_surface_names_the_view_a_move_was_dispatched_at) {
  HoverTracker tracker;
  EXPECT(tracker.admitMove(10, kListening));
  // A leave is dispatched at whatever React Native was last told about; the
  // processor reads it as the pointer being gone and unwinds the path itself.
  EXPECT_EQ(tracker.admitLeave(), 10);
  // And a second leave is not a second event.
  EXPECT_EQ(tracker.admitLeave(), 0);
}

TEST(hover_leaving_without_ever_dispatching_is_nothing) {
  HoverTracker tracker;
  tracker.admitMove(10, kSilent);
  // React Native has no path to unwind, so there is nothing to tell it.
  EXPECT_EQ(tracker.admitLeave(), 0);
}

TEST(hover_forget_drops_the_state_without_naming_anything) {
  HoverTracker tracker;
  tracker.admitMove(10, kListening);
  // For a surface teardown: the view is about to stop existing, and a leave
  // dispatched at it would be a lookup of an emitter that is gone.
  tracker.forget();
  EXPECT_EQ(tracker.admitLeave(), 0);
  EXPECT(!tracker.admitMove(11, kSilent));
}

TEST(hover_listeners_come_off_the_props_react_native_parsed) {
  facebook::react::ViewEvents events{};
  EXPECT_EQ(basalt::hoverListenersFrom(events), (std::uint16_t)basalt::HoverListenerNone);

  events[facebook::react::ViewEvents::Offset::PointerEnter] = true;
  events[facebook::react::ViewEvents::Offset::PointerOutCapture] = true;
  EXPECT_EQ(basalt::hoverListenersFrom(events),
            (std::uint16_t)(basalt::HoverListenerEnter | basalt::HoverListenerOutCapture));
}
