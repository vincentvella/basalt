// Hit testing.
//
// The same questions tests/test_hittest.cpp asks of GTK and the hit-test half
// of tests/test_appkit_input.mm asks of AppKit, in the same order, plus two
// this platform has to answer for itself.
//
// Both other hosts inherit picking from their toolkit -- `gtk_widget_pick` and
// `-[NSView hitTest:]` -- so the work there is making the toolkit's geometry
// agree with Fabric's. There is no toolkit geometry here, so this is written
// out, which is more code and one fewer thing that can silently disagree: the
// transform a view is drawn with is the transform a press is tested against,
// because `localToParent` is the only place either of them is composed.

#include "TestHarness.h"

#include "RnWin32View.h"
#include "Win32Cursors.h"
#include "Win32InputScopes.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using basalt::win32::hitTest;
using basalt::win32::RnWin32View;

namespace {

class Tree {
 public:
  RnWin32View *box(int32_t tag, float x, float y, float w, float h) {
    auto view = std::make_unique<RnWin32View>(tag);
    view->setFrame(x, y, w, h);
    RnWin32View *raw = view.get();
    views_.push_back(std::move(view));
    return raw;
  }

 private:
  std::vector<std::unique_ptr<RnWin32View>> views_;
};

// The tag of whatever is under a point, or -1 for a miss. Comparing tags rather
// than pointers makes a failure say which view it found.
int32_t tagAt(RnWin32View *root, float x, float y) {
  RnWin32View *hit = hitTest(root, x, y);
  return hit == nullptr ? -1 : hit->tag();
}

} // namespace

TEST(hit_test_finds_the_view_under_a_point) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  root->insertChild(tree.box(2, 50, 50, 100, 100), 0);

  EXPECT_EQ(tagAt(root, 100, 100), 2);
}

TEST(hit_test_uses_top_left_coordinates) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  // Near the top of the root. On a coordinate system with the origin at the
  // bottom -- AppKit's, unflipped -- this point would be over nothing.
  root->insertChild(tree.box(2, 0, 0, 400, 40), 0);

  EXPECT_EQ(tagAt(root, 200, 20), 2);
  EXPECT_EQ(tagAt(root, 200, 380), 1);
}

TEST(hit_test_misses_return_the_root_not_a_child) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  root->insertChild(tree.box(2, 50, 50, 100, 100), 0);

  EXPECT_EQ(tagAt(root, 300, 300), 1);
}

TEST(hit_test_outside_the_root_is_a_miss) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);

  EXPECT_EQ(tagAt(root, 500, 200), -1);
  EXPECT_EQ(tagAt(root, -1, 200), -1);
}

TEST(hit_test_returns_the_deepest_view) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *middle = tree.box(2, 50, 50, 200, 200);
  middle->insertChild(tree.box(3, 25, 25, 50, 50), 0);
  root->insertChild(middle, 0);

  // (100,100) is inside all three; the innermost wins.
  EXPECT_EQ(tagAt(root, 100, 100), 3);
}

TEST(hit_test_respects_sibling_order) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  root->insertChild(tree.box(2, 0, 0, 200, 200), 0);
  root->insertChild(tree.box(3, 0, 0, 200, 200), 1);

  // Exactly overlapping. The later child is painted on top, so it is the one a
  // press lands on.
  EXPECT_EQ(tagAt(root, 100, 100), 3);
}

TEST(hit_test_follows_z_index) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *under = tree.box(2, 0, 0, 200, 200);
  RnWin32View *over = tree.box(3, 0, 0, 200, 200);
  root->insertChild(under, 0);
  root->insertChild(over, 1);

  // A negative zIndex on the later child puts it underneath, and a press has to
  // follow the paint order rather than the child list -- otherwise a view can
  // be drawn behind another and still swallow its clicks.
  over->setZIndex(-1);
  EXPECT_EQ(tagAt(root, 100, 100), 2);
}

TEST(hit_test_follows_a_scroll_offset) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *scroller = tree.box(50, 0, 0, 200, 200);
  scroller->insertChild(tree.box(51, 0, 300, 200, 100), 0);
  root->insertChild(scroller, 0);

  // The row sits below the viewport, so nothing of it is under this point.
  EXPECT_EQ(tagAt(root, 100, 50), 50);

  scroller->setScrollOffset(0, 300);
  EXPECT_EQ(tagAt(root, 100, 50), 51);
}

TEST(hit_test_skips_hidden_views) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *hidden = tree.box(2, 0, 0, 200, 200);
  root->insertChild(hidden, 0);

  EXPECT_EQ(tagAt(root, 100, 100), 2);

  // `display: none` takes a view out of painting and out of picking together.
  // Distinct from opacity 0, which is still there to be pressed.
  hidden->setHidden(true);
  EXPECT_EQ(tagAt(root, 100, 100), 1);
}

TEST(hit_test_skips_the_children_of_a_hidden_view) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *hidden = tree.box(2, 0, 0, 200, 200);
  hidden->insertChild(tree.box(3, 0, 0, 100, 100), 0);
  root->insertChild(hidden, 0);

  hidden->setHidden(true);
  EXPECT_EQ(tagAt(root, 50, 50), 1);
}

// `backfaceVisibility: 'hidden'` takes the view out of picking as well as out
// of the picture -- the back of a card is not clickable.
TEST(hit_test_skips_a_view_turned_away) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *card = tree.box(2, 0, 0, 200, 200);
  card->setHidesBackFace(true);
  root->insertChild(card, 0);

  EXPECT_EQ(tagAt(root, 100, 100), 2);

  // Half a turn about Y, which mirrors it.
  const float flipped[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
  card->setTransform(flipped);
  EXPECT_EQ(tagAt(root, 100, 100), 1);

  // Turning the prop off shows it again. The early return this used to have
  // left the view hidden for good.
  card->setHidesBackFace(false);
  EXPECT_EQ(tagAt(root, 100, 100), 2);
}

// The two reasons a view can be hidden are independent, and Fabric applies
// them through different calls: props first, then layout metrics. So a card
// turned away from the viewer had `display: none`'s "no" written over the top
// of it on the very same mount, and came back.
TEST(display_none_and_a_back_face_do_not_cancel_each_other) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *card = tree.box(2, 0, 0, 200, 200);
  card->setHidesBackFace(true);
  root->insertChild(card, 0);

  const float flipped[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};
  card->setTransform(flipped);
  EXPECT_EQ(tagAt(root, 100, 100), 1);

  // What Win32MountingManager says about every view that is not display: none.
  card->setHidden(false);
  EXPECT_EQ(tagAt(root, 100, 100), 1);

  // And the other way: facing the viewer must not reveal what the app hid.
  card->setHidden(true);
  card->setTransform(nullptr);
  EXPECT_EQ(tagAt(root, 100, 100), 1);
}

TEST(hit_test_follows_a_transform) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  // The same wide, short card the paint tests rotate, at the same coordinates,
  // so the two suites corroborate rather than each asserting its own idea of
  // where the thing went.
  RnWin32View *card = tree.box(2, 10, 40, 80, 20);
  root->insertChild(card, 0);

  EXPECT_EQ(tagAt(root, 20, 50), 2);
  EXPECT_EQ(tagAt(root, 50, 20), 1);

  // A quarter turn about the centre: the card now covers x 40..60, y 10..90.
  const float quarterTurn[16] = {
      0, 1, 0, 0, //
      -1, 0, 0, 0, //
      0, 0, 1, 0, //
      0, 0, 0, 1};
  card->setTransform(quarterTurn);

  // This is the assertion the whole inverse-matrix path exists for. A platform
  // that draws the transform but does not pick through it leaves a rotated
  // button clickable where it used to be, and both of these still pass the
  // *first* way round -- which is why both are here.
  EXPECT_EQ(tagAt(root, 50, 20), 2);
  EXPECT_EQ(tagAt(root, 20, 50), 1);
}

TEST(hit_test_on_a_null_root_is_a_miss) {
  EXPECT_EQ(tagAt(nullptr, 10, 10), -1);
}

// `pointerEvents`, which decides what a press can land on rather than what is
// drawn. The same four cases the GTK and AppKit suites ask, in the same order,
// because the answer has to be the same on all three.
TEST(pointer_events_none_passes_a_press_through_to_what_is_behind) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *behind = tree.box(10, 0, 0, 200, 200);
  RnWin32View *over = tree.box(11, 0, 0, 200, 200);
  root->insertChild(behind, 0);
  root->insertChild(over, 1);

  // Painted last, so it is on top and takes the press.
  EXPECT_EQ(tagAt(root, 50, 50), 11);

  over->setPointerEvents(RnWin32View::PointerEvents::None);
  EXPECT_EQ(tagAt(root, 50, 50), 10);
}

TEST(pointer_events_none_takes_the_children_with_it) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *over = tree.box(11, 0, 0, 200, 200);
  over->insertChild(tree.box(12, 0, 0, 100, 100), 0);
  root->insertChild(over, 0);

  EXPECT_EQ(tagAt(root, 50, 50), 12);
  // The whole subtree leaves hit testing, not just the view the prop is on.
  over->setPointerEvents(RnWin32View::PointerEvents::None);
  EXPECT_EQ(tagAt(root, 50, 50), 1);
}

TEST(pointer_events_box_none_is_transparent_and_its_children_are_not) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *behind = tree.box(10, 0, 0, 300, 300);
  RnWin32View *overlay = tree.box(11, 0, 0, 300, 300);
  overlay->insertChild(tree.box(12, 0, 0, 100, 100), 0);
  root->insertChild(behind, 0);
  root->insertChild(overlay, 1);
  overlay->setPointerEvents(RnWin32View::PointerEvents::BoxNone);

  // Over the child: the overlay's children are still targets.
  EXPECT_EQ(tagAt(root, 50, 50), 12);
  // Over the overlay and nothing inside it: the press belongs to the view
  // *behind*, not to the overlay's parent. That is the whole reason the mode
  // exists, and returning the parent would look right until something was
  // underneath.
  EXPECT_EQ(tagAt(root, 200, 200), 10);
}

TEST(pointer_events_box_only_swallows_presses_meant_for_its_children) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *panel = tree.box(11, 0, 0, 300, 300);
  panel->insertChild(tree.box(12, 0, 0, 100, 100), 0);
  root->insertChild(panel, 0);

  EXPECT_EQ(tagAt(root, 50, 50), 12);
  panel->setPointerEvents(RnWin32View::PointerEvents::BoxOnly);
  // The press lands on the panel even though it is over the child, which is
  // what makes a disabled panel disable everything in it.
  EXPECT_EQ(tagAt(root, 50, 50), 11);
  EXPECT_EQ(tagAt(root, 200, 200), 11);
}

TEST(pointer_events_shows_up_in_the_tree_dump) {
  Tree tree;
  RnWin32View *view = tree.box(10, 0, 0, 100, 100);
  view->setPointerEvents(RnWin32View::PointerEvents::BoxNone);
  // Printed because it is invisible otherwise: a view with this prop is drawn
  // exactly like one without, so the cross-host diff can only see the prop
  // arrived if each host prints it.
  EXPECT(view->describeTree().find("pe=box-none") != std::string::npos);
}

// --------------------------------------------------------------------------
// offsetPoint
// --------------------------------------------------------------------------
//
// `pageToLocal` is what `Touch::offsetPoint` is built from, and it inverts the
// same chain `hitTest` above inverts on the way down. The AppKit suite has the
// same five cases against `rnPageToLocal:fromRoot:into:`.

TEST(page_to_local_subtracts_the_ancestors) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *middle = tree.box(10, 50, 60, 300, 300);
  RnWin32View *leaf = tree.box(20, 20, 30, 100, 100);
  root->insertChild(middle, 0);
  middle->insertChild(leaf, 0);

  float x = 0.0f;
  float y = 0.0f;
  EXPECT(leaf->pageToLocal(root, 100.0f, 120.0f, x, y));
  // 100 - 50 - 20, and 120 - 60 - 30.
  EXPECT_NEAR(x, 30.0f, 0.001f);
  EXPECT_NEAR(y, 30.0f, 0.001f);
}

// The root's own point is the page point, which is the case that made the old
// behaviour look right for as long as it did.
TEST(page_to_local_at_the_root_is_the_page_point) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);

  float x = 0.0f;
  float y = 0.0f;
  EXPECT(root->pageToLocal(root, 100.0f, 120.0f, x, y));
  EXPECT_NEAR(x, 100.0f, 0.001f);
  EXPECT_NEAR(y, 120.0f, 0.001f);
}

TEST(page_to_local_follows_a_transformed_ancestor) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *moved = tree.box(10, 0, 0, 100, 100);
  // Shifted 100 right and 50 down by a transform rather than by its frame.
  const float translate[16] = {
      1, 0, 0, 0, //
      0, 1, 0, 0, //
      0, 0, 1, 0, //
      100, 50, 0, 1};
  moved->setTransform(translate);
  root->insertChild(moved, 0);

  float x = 0.0f;
  float y = 0.0f;
  EXPECT(moved->pageToLocal(root, 110.0f, 60.0f, x, y));
  // The press is 10 into the view as drawn, not 110.
  EXPECT_NEAR(x, 10.0f, 0.001f);
  EXPECT_NEAR(y, 10.0f, 0.001f);
}

// A scrolled ancestor moves its children, and the offset has to come back out
// -- the same offset `hitTest` adds on the way down.
TEST(page_to_local_follows_a_scrolled_ancestor) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *scroller = tree.box(10, 0, 0, 200, 200);
  RnWin32View *row = tree.box(20, 0, 300, 200, 50);
  root->insertChild(scroller, 0);
  scroller->insertChild(row, 0);
  scroller->setScrollOffset(0.0f, 280.0f);

  float x = 0.0f;
  float y = 0.0f;
  // The row starts at 300 in content space and the view is scrolled 280, so it
  // is drawn 20 down -- and a press there is at the row's own top.
  EXPECT(row->pageToLocal(root, 10.0f, 20.0f, x, y));
  EXPECT_NEAR(x, 10.0f, 0.001f);
  EXPECT_NEAR(y, 0.0f, 0.001f);
}

TEST(page_to_local_refuses_a_chain_with_no_inverse) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *flat = tree.box(10, 0, 0, 100, 100);
  const float flattened[16] = {
      0, 0, 0, 0, //
      0, 0, 0, 0, //
      0, 0, 1, 0, //
      0, 0, 0, 1};
  flat->setTransform(flattened);
  root->insertChild(flat, 0);

  float x = -1.0f;
  float y = -1.0f;
  EXPECT(!flat->pageToLocal(root, 10.0f, 10.0f, x, y));
  // Untouched, so the caller's fallback is what is used.
  EXPECT_NEAR(x, -1.0f, 0.001f);
}

// ---------------------------------------------------------------------------
// `hitSlop`: how far outside its own box a view answers a press.
//
// The same four questions tests/test_hittest.cpp asks of GTK, in the same
// order, and asserted the same way: through the hit test every press goes
// through rather than against the stored insets, because a view that remembered
// its slop and did not widen the test would leave every target the size it was.
// ---------------------------------------------------------------------------

TEST(hit_slop_grows_the_target_outside_the_box) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *icon = tree.box(60, 100, 100, 20, 20);
  root->insertChild(icon, 0);
  const float insets[4] = {12.0f, 12.0f, 12.0f, 12.0f};
  icon->setHitSlop(insets);

  // Inside the box, as before.
  EXPECT_EQ(tagAt(root, 110, 110), 60);
  // Ten points outside it on each side, which is inside the slop.
  EXPECT_EQ(tagAt(root, 92, 110), 60);
  EXPECT_EQ(tagAt(root, 128, 110), 60);
  EXPECT_EQ(tagAt(root, 110, 92), 60);
  EXPECT_EQ(tagAt(root, 110, 128), 60);
  // And past it, which is the root again.
  EXPECT_EQ(tagAt(root, 80, 110), 1);
}

// Each edge on its own, because four numbers in one struct is four chances to
// read one into the wrong side: a slop that grew the top when the app asked for
// the bottom would pass any symmetric test.
TEST(hit_slop_applies_each_edge_where_it_was_asked_for) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *view = tree.box(61, 100, 100, 20, 20);
  root->insertChild(view, 0);

  const float top[4] = {15.0f, 0.0f, 0.0f, 0.0f};
  view->setHitSlop(top);
  EXPECT_EQ(tagAt(root, 110, 90), 61);
  // Not the other three.
  EXPECT_EQ(tagAt(root, 110, 130), 1);
  EXPECT_EQ(tagAt(root, 90, 110), 1);
  EXPECT_EQ(tagAt(root, 130, 110), 1);

  // And the opposite edge, through the same view: the prop can change.
  const float bottom[4] = {0.0f, 0.0f, 15.0f, 0.0f};
  view->setHitSlop(bottom);
  EXPECT_EQ(tagAt(root, 110, 130), 61);
  EXPECT_EQ(tagAt(root, 110, 90), 1);
}

// Taken away again. A slop left behind is a view that swallows presses meant for
// its neighbour, which is harder to notice than a target that is too small.
TEST(hit_slop_can_be_taken_away_again) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *view = tree.box(62, 100, 100, 20, 20);
  root->insertChild(view, 0);

  const float insets[4] = {12.0f, 12.0f, 12.0f, 12.0f};
  view->setHitSlop(insets);
  EXPECT_EQ(tagAt(root, 92, 110), 62);

  view->setHitSlop(nullptr);
  EXPECT_EQ(tagAt(root, 92, 110), 1);
  EXPECT_EQ(tagAt(root, 110, 110), 62);
}

// A slop that overlaps a sibling does not win over it: the sibling is drawn on
// top, and a target that reached under something visible would take presses
// meant for it.
TEST(hit_slop_does_not_beat_a_view_drawn_over_it) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *first = tree.box(63, 100, 100, 20, 20);
  root->insertChild(first, 0);
  const float insets[4] = {0.0f, 40.0f, 0.0f, 0.0f};
  first->setHitSlop(insets);

  RnWin32View *second = tree.box(64, 130, 100, 20, 20);
  root->insertChild(second, 1);

  // Inside the slop and outside the sibling: the slop answers.
  EXPECT_EQ(tagAt(root, 125, 110), 63);
  // Inside both: the sibling, which is on top.
  EXPECT_EQ(tagAt(root, 140, 110), 64);
}

// And it is in the tree dump, which is the only way to see it: a view with a
// bigger target is drawn exactly like one without. Spelled as the other two
// hosts spell it, because the dump is diffed across the three.
TEST(hit_slop_is_reported_in_the_tree) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *view = tree.box(65, 100, 100, 20, 20);
  root->insertChild(view, 0);

  EXPECT(root->describeTree().find("hit-slop=") == std::string::npos);

  const float insets[4] = {16.0f, 8.0f, 4.0f, 2.0f};
  view->setHitSlop(insets);
  EXPECT(root->describeTree().find("hit-slop=(16,8,4,2)") != std::string::npos);

  // Zeroes are not a slop, so nothing is printed for a view that asked for
  // none: the other two hosts print nothing there either.
  const float none[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  view->setHitSlop(none);
  EXPECT(root->describeTree().find("hit-slop=") == std::string::npos);
}

// ---------------------------------------------------------------------------
// The `cursor` style property, which is a hit-test question on this platform.
//
// Win32 has no per-view cursor: the window answers `WM_SETCURSOR` with whatever
// is under the pointer, so "which cursor" is "which view", and that is what
// these ask. The GTK and AppKit hosts hand the keyword to their toolkit and get
// the inheritance for free, which is why this is the only suite that has to say
// what inheritance means.
// ---------------------------------------------------------------------------

TEST(cursor_is_the_innermost_view_that_asked_for_one) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *button = tree.box(2, 50, 50, 100, 100);
  RnWin32View *label = tree.box(3, 10, 10, 20, 20);
  button->setCursor("pointer");
  label->setCursor("text");
  button->insertChild(label, 0);
  root->insertChild(button, 0);

  // Over the label: its own keyword.
  EXPECT_EQ(basalt::win32::cursorNameAt(root, 70, 70), std::string("text"));
  // Over the button but not the label: the button's.
  EXPECT_EQ(basalt::win32::cursorNameAt(root, 120, 120), std::string("pointer"));
  // Over neither: nothing, which is what leaves the pointer alone.
  EXPECT(basalt::win32::cursorNameAt(root, 300, 300).empty());
}

// CSS's cursor inherits, so a child that asked for nothing shows its parent's.
// A host that only read the view under the pointer would answer nothing here,
// which is an arrow over the middle of a button.
TEST(cursor_is_inherited_by_a_child_that_asked_for_nothing) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *button = tree.box(2, 50, 50, 100, 100);
  RnWin32View *label = tree.box(3, 10, 10, 20, 20);
  button->setCursor("pointer");
  button->insertChild(label, 0);
  root->insertChild(button, 0);

  EXPECT_EQ(basalt::win32::cursorNameAt(root, 70, 70), std::string("pointer"));

  // And taken away again: `cursor: 'auto'` arrives as no keyword at all.
  button->setCursor(nullptr);
  EXPECT(basalt::win32::cursorNameAt(root, 70, 70).empty());
}

// A view a press passes through does not change the pointer either, which falls
// out of using the same hit test for both and is worth pinning: the two must
// not disagree about what the pointer is over.
TEST(cursor_follows_pointer_events_like_a_press_does) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  RnWin32View *under = tree.box(2, 50, 50, 100, 100);
  RnWin32View *over = tree.box(3, 50, 50, 100, 100);
  under->setCursor("pointer");
  over->setCursor("text");
  over->setPointerEvents(RnWin32View::PointerEvents::None);
  root->insertChild(under, 0);
  root->insertChild(over, 1);

  EXPECT_EQ(basalt::win32::cursorNameAt(root, 70, 70), std::string("pointer"));
}

TEST(cursor_is_reported_in_the_tree) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 400, 400);
  EXPECT(root->describeTree().find("cursor=") == std::string::npos);

  root->setCursor("ns-resize");
  // The keyword, as the other two hosts print it: one end-to-end scenario reads
  // this line on all three, and the hyphenated name is where a mapping table
  // goes wrong.
  EXPECT(root->describeTree().find("cursor=ns-resize") != std::string::npos);
}

// And the Win32 half: a keyword as a cursor the system has.
//
// Against `LoadCursorW` rather than a table of names, because the table is the
// thing under test: a keyword that landed on a cursor the system does not have
// answers null, and the assertions below would pass on an empty map if they
// only checked that nothing crashed. So each group is also compared against the
// others, which is what says `ns-resize` and `ew-resize` are not the same arrow.
TEST(cursor_keywords_map_onto_the_cursors_windows_has) {
  const auto loaded = [](const char *keyword) {
    return basalt::win32::win32CursorFor(keyword).cursor;
  };

  EXPECT(loaded("default") == LoadCursorW(nullptr, IDC_ARROW));
  EXPECT(loaded("pointer") == LoadCursorW(nullptr, IDC_HAND));
  EXPECT(loaded("text") == LoadCursorW(nullptr, IDC_IBEAM));
  EXPECT(loaded("crosshair") == LoadCursorW(nullptr, IDC_CROSS));
  EXPECT(loaded("wait") == LoadCursorW(nullptr, IDC_WAIT));
  EXPECT(loaded("progress") == LoadCursorW(nullptr, IDC_APPSTARTING));
  EXPECT(loaded("help") == LoadCursorW(nullptr, IDC_HELP));
  EXPECT(loaded("not-allowed") == LoadCursorW(nullptr, IDC_NO));

  // The families, which share a cursor because Windows draws no difference --
  // and the eight resize keywords collapse onto four arrows the way a window's
  // own edges do.
  EXPECT(loaded("grab") == loaded("move"));
  EXPECT(loaded("grabbing") == loaded("all-scroll"));
  EXPECT(loaded("ew-resize") == loaded("col-resize"));
  EXPECT(loaded("ns-resize") == loaded("row-resize"));
  EXPECT(loaded("ne-resize") == loaded("nesw-resize"));
  EXPECT(loaded("nw-resize") == loaded("nwse-resize"));

  // And the four resize arrows are four different arrows.
  EXPECT(loaded("ew-resize") != loaded("ns-resize"));
  EXPECT(loaded("nesw-resize") != loaded("nwse-resize"));
  EXPECT(loaded("ew-resize") != loaded("nesw-resize"));
}

TEST(cursor_none_hides_the_pointer_rather_than_loading_one) {
  const basalt::win32::Win32Cursor none = basalt::win32::win32CursorFor("none");
  EXPECT(none.hidden);
  EXPECT(none.cursor == nullptr);

  // No keyword at all is neither: the pointer is left as it is.
  const basalt::win32::Win32Cursor nothing = basalt::win32::win32CursorFor("");
  EXPECT(!nothing.hidden);
  EXPECT(nothing.cursor == nullptr);
}

// The keywords the stock set has nothing for, which is a decision rather than
// an omission: the art exists inside shell32 as unnamed ordinals, and loading
// an ordinal out of a system DLL works until an update moves it. The dump still
// reports the keyword, so what the app asked for is not lost.
TEST(cursor_a_keyword_windows_has_nothing_for_leaves_the_pointer_alone) {
  for (const char *keyword : {"alias", "cell", "context-menu", "copy", "zoom-in", "zoom-out"}) {
    const basalt::win32::Win32Cursor answer = basalt::win32::win32CursorFor(keyword);
    EXPECT(answer.cursor == nullptr);
    EXPECT(!answer.hidden);
  }
}

// ---------------------------------------------------------------------------
// `keyboardType` as a Windows input scope. See Win32InputScopes.h.
//
// A table, so a table test: what is worth asserting is that each name lands on
// the scope that means the same thing, and that the ones Windows draws no
// distinction between are the ones that share a scope. Here rather than in the
// <TextInput> suite because the mapping carries no React Native, which is also
// why it is keyed on the name core prints rather than on the enum.
// ---------------------------------------------------------------------------

TEST(input_scope_follows_the_keyboard_type_a_field_asked_for) {
  const auto scope = [](const char *name) {
    return basalt::win32::inputScopeForKeyboardType(name);
  };

  EXPECT_EQ(static_cast<int>(scope("email-address")),
            static_cast<int>(IS_EMAIL_SMTPEMAILADDRESS));
  EXPECT_EQ(static_cast<int>(scope("url")), static_cast<int>(IS_URL));
  EXPECT_EQ(static_cast<int>(scope("phone-pad")),
            static_cast<int>(IS_TELEPHONE_FULLTELEPHONENUMBER));
  // A number that may carry a separator against digits and nothing else, which
  // is the one distinction in this table that is not just a rename.
  EXPECT_EQ(static_cast<int>(scope("numeric")), static_cast<int>(IS_NUMBER));
  EXPECT_EQ(static_cast<int>(scope("decimal-pad")), static_cast<int>(IS_NUMBER));
  EXPECT_EQ(static_cast<int>(scope("number-pad")), static_cast<int>(IS_DIGITS));
  EXPECT(scope("numeric") != scope("number-pad"));

  // `default` and the names that are iOS's own vocabulary: ordinary text, which
  // is more useful than refusing to answer and is what the comment there says.
  EXPECT_EQ(static_cast<int>(scope("default")), static_cast<int>(IS_DEFAULT));
  EXPECT_EQ(static_cast<int>(scope("twitter")), static_cast<int>(IS_DEFAULT));
  EXPECT_EQ(static_cast<int>(scope("ascii-capable")), static_cast<int>(IS_DEFAULT));
  // And a name this table has never heard of, which is what a future React
  // Native adding one looks like.
  EXPECT_EQ(static_cast<int>(scope("something-new")), static_cast<int>(IS_DEFAULT));
}

// What a field asked for about its text reaches the tree dump, which is where
// an app can see that a prop arrived on a host that cannot act on it -- and
// what the end-to-end scenario reads on all three.
TEST(input_kinds_and_text_checking_are_reported_in_the_tree) {
  Tree tree;
  RnWin32View *view = tree.box(1, 0, 0, 100, 40);

  // Nothing said: nothing printed. Unset spelling is a third state rather than
  // off, and a host that resolved it to false would mark every field.
  EXPECT(view->describeTree().find("spellcheck=") == std::string::npos);
  EXPECT(view->describeTree().find("autocorrect=") == std::string::npos);
  EXPECT(view->describeTree().find("keyboard=") == std::string::npos);

  view->setTextChecking("off", "off");
  view->setInputKinds("none", "email-address");
  const std::string dump = view->describeTree();
  EXPECT(dump.find("spellcheck=off") != std::string::npos);
  EXPECT(dump.find("autocorrect=off") != std::string::npos);
  EXPECT(dump.find("autocapitalize=none") != std::string::npos);
  EXPECT(dump.find("keyboard=email-address") != std::string::npos);

  // And taken away again, which is what a field that stops saying anything
  // means: core answers a null name for an unset flag.
  view->setTextChecking(nullptr, nullptr);
  const std::string quiet = view->describeTree();
  EXPECT(quiet.find("spellcheck=") == std::string::npos);
  EXPECT(quiet.find("autocorrect=") == std::string::npos);
  // The other two are plain enums with React Native's own defaults, so they
  // stay: a field always has a capitalisation and a keyboard.
  EXPECT(quiet.find("autocapitalize=none") != std::string::npos);
}
