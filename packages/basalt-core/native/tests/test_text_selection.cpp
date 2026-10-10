// What a press and a drag mean inside selectable text.
//
// The selection itself is two integers and an ordering; what is worth asserting
// is the arbitration, because that is where a wrong answer breaks something
// else. A press that claimed immediately would stop a `<Pressable>` wrapping
// the text from ever firing, and a press that never claimed would scroll the
// list instead of selecting. Both are invisible in a screenshot.

#include "TestHarness.h"

#include "TextSelection.h"

#include <sstream>
#include <string>

using basalt::TextSelection;
using facebook::react::Point;

namespace {

// A paragraph's tag, for readability: the number itself means nothing.
constexpr facebook::react::Tag kParagraph = 7;
constexpr facebook::react::Tag kOtherParagraph = 9;

} // namespace

TEST(selection_a_press_selects_nothing) {
  // The whole arbitration rests on this: a press is a tap until it moves, so
  // nothing is selected and nothing is claimed.
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});
  EXPECT(!selection.dragging());
  EXPECT(!selection.active());
  EXPECT(selection.range().empty());
  EXPECT_EQ((long)selection.tag(), 0L);
}

TEST(selection_a_small_move_is_still_a_tap) {
  // A cursor delivers sub-pixel motion and a hand shakes. Claiming here would
  // cancel a touch sequence that is still a press.
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});
  EXPECT(!selection.moveTo(5, Point{11, 10}));
  EXPECT(!selection.moveTo(5, Point{10, 11.5}));
  EXPECT(!selection.active());
  EXPECT(selection.range().empty());
}

TEST(selection_a_move_past_the_threshold_claims_the_gesture_once) {
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});

  // True the once: the caller cancels the touch sequence on this edge, and
  // cancelling twice would be a second touchcancel for one gesture.
  EXPECT(selection.moveTo(9, Point{40, 10}));
  EXPECT(!selection.moveTo(12, Point{60, 10}));
  EXPECT(!selection.moveTo(14, Point{80, 10}));

  EXPECT(selection.dragging());
  EXPECT(selection.active());
  EXPECT_EQ((long)selection.tag(), (long)kParagraph);
  EXPECT_EQ(selection.range().start, 4);
  EXPECT_EQ(selection.range().length, 10);
}

TEST(selection_a_drag_backwards_selects_the_same_text) {
  // Anchor and focus are where the gesture started and where it is now, in that
  // order; a range is the two of them sorted. Reporting a negative length to a
  // highlight would draw nothing and look like the feature was off.
  TextSelection selection;
  selection.press(kParagraph, 12, Point{80, 10});
  EXPECT(selection.moveTo(4, Point{40, 10}));
  EXPECT_EQ(selection.range().start, 4);
  EXPECT_EQ(selection.range().length, 8);
}

TEST(selection_a_release_keeps_the_selection_and_ends_the_drag) {
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});
  EXPECT(selection.moveTo(9, Point{40, 10}));
  selection.release();

  // The highlight stays: a person lets go of the mouse and expects the text to
  // stay selected until they click somewhere.
  EXPECT(selection.active());
  EXPECT_EQ(selection.range().length, 5);
  // And moving afterwards does not extend it, the button being up.
  EXPECT(!selection.dragging());
  EXPECT(!selection.moveTo(20, Point{200, 10}));
  EXPECT_EQ(selection.range().length, 5);
}

TEST(selection_a_tap_clears_what_was_selected) {
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});
  EXPECT(selection.moveTo(9, Point{40, 10}));
  selection.release();
  EXPECT(selection.active());

  // On the press rather than the release: a highlight that survived the click
  // meant to dismiss it until the button came up again would flicker.
  selection.press(kParagraph, 2, Point{20, 10});
  EXPECT(!selection.active());
  EXPECT(selection.range().empty());
}

TEST(selection_a_press_outside_selectable_text_cannot_become_one) {
  // Tag 0 is "nothing selectable here", which is what a host passes for a press
  // on a view that is not a selectable paragraph. Every later move has to stay
  // a touch, or dragging over a plain view would cancel its own press.
  TextSelection selection;
  selection.press(0, 0, Point{10, 10});
  EXPECT(!selection.moveTo(5, Point{80, 10}));
  EXPECT(!selection.dragging());
  EXPECT(!selection.active());
}

TEST(selection_a_press_in_another_paragraph_moves_it) {
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});
  EXPECT(selection.moveTo(9, Point{40, 10}));
  selection.release();

  selection.press(kOtherParagraph, 1, Point{10, 100});
  EXPECT(selection.moveTo(6, Point{50, 100}));
  EXPECT_EQ((long)selection.tag(), (long)kOtherParagraph);
  EXPECT_EQ(selection.range().start, 1);
  EXPECT_EQ(selection.range().length, 5);
}

TEST(selection_a_drag_that_comes_back_to_where_it_started_selects_nothing) {
  // Claimed and empty, which is a real state: the gesture is a selection -- the
  // touch sequence was cancelled and cannot be un-cancelled -- and there is
  // nothing to draw. A host asking `active()` gets false and draws nothing,
  // which is what matters.
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});
  EXPECT(selection.moveTo(9, Point{40, 10}));
  EXPECT(!selection.moveTo(4, Point{10, 10}));
  EXPECT(selection.dragging());
  EXPECT(!selection.active());
  EXPECT(selection.range().empty());
}

TEST(selection_clearing_forgets_the_paragraph_as_well) {
  // What a mutation calls when the text it was selecting is gone: an index into
  // a paragraph that has been re-laid-out means nothing, and a highlight drawn
  // from it would be in the wrong place rather than absent.
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});
  EXPECT(selection.moveTo(9, Point{40, 10}));
  selection.clear();
  EXPECT(!selection.active());
  EXPECT(!selection.dragging());
  EXPECT_EQ((long)selection.tag(), 0L);
  EXPECT(selection.range().empty());
}

TEST(selection_a_vertical_drag_claims_too) {
  // The threshold is a distance rather than a horizontal one: selecting from
  // one line to the next is mostly vertical motion, and a sweep down a
  // paragraph has to claim as readily as a sweep across one.
  TextSelection selection;
  selection.press(kParagraph, 4, Point{10, 10});
  EXPECT(selection.moveTo(40, Point{10, 40}));
  EXPECT(selection.active());
  EXPECT_EQ(selection.range().length, 36);
}
