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

#include <memory>
#include <sstream>
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
