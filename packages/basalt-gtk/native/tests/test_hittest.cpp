// Tests for hit testing.
//
// This is the part of input most likely to be quietly wrong, and it is a pure
// function of the widget tree, so it can be tested without a gesture, a
// display server, or permission to synthesise a pointer event. That matters:
// synthesising a real click needs accessibility permission an automated run
// does not have, so everything below GDK is tested here instead.

#include "TestHarness.h"

#include "GtkMountingManager.h"
#include "GtkTouchDispatcher.h"
#include "RnView.h"

#include <sstream>

namespace {

RnView *addChild(RnView *parent, int tag, float x, float y, float width, float height) {
  RnView *child = rn_view_new(tag);
  g_object_ref_sink(child);
  rn_view_set_frame(child, x, y, width, height);
  rn_view_insert_child(parent, child, 0);
  return child;
}

// gtk_widget_pick skips widgets that are not mapped, and a widget is only
// mapped inside a window that has been shown. So these tests put the tree in a
// real window rather than allocating it in isolation the way the layout tests
// do -- which is also closer to what happens when a pointer actually arrives.
//
// Allocation happens in the frame clock's layout phase, not synchronously when
// a child is added, so every structural change is followed by a pump that
// spins the main loop until a frame has actually gone through. Draining the
// context without waiting is not enough: with no frame due, the iteration
// returns immediately and the child is still unallocated.
struct Scene {
  GtkWidget *window;
  RnView *root;

  explicit Scene(int width, int height) {
    root = rn_view_new(1);
    window = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(root));
  }

  ~Scene() { gtk_window_destroy(GTK_WINDOW(window)); }

  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  // Call once the tree is built.
  void show() {
    gtk_widget_set_visible(window, TRUE);
    pump();
  }

  // Spins the main loop for long enough that a frame is drawn and the tree is
  // allocated. Bounded, so a machine that never produces a frame fails the
  // assertion rather than hanging the suite.
  static void pump() {
    const gint64 deadline = g_get_monotonic_time() + 500000; // 500ms
    while (g_get_monotonic_time() < deadline) {
      while (g_main_context_iteration(nullptr, FALSE)) {
      }
      g_usleep(1000);
    }
  }
};

} // namespace

TEST(hit_test_finds_the_view_under_a_point) {
  Scene scene(400, 400);
  addChild(scene.root, 20, 10.0F, 10.0F, 100.0F, 100.0F);
  scene.show();

  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 20);
}

TEST(hit_test_misses_return_the_root_not_a_child) {
  Scene scene(400, 400);
  addChild(scene.root, 21, 10.0F, 10.0F, 50.0F, 50.0F);
  scene.show();

  // Outside the child but inside the root.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 300.0, 300.0)), 1);
}

TEST(hit_test_returns_the_deepest_view) {
  Scene scene(400, 400);
  RnView *outer = addChild(scene.root, 30, 0.0F, 0.0F, 200.0F, 200.0F);
  addChild(outer, 31, 20.0F, 20.0F, 60.0F, 60.0F);
  scene.show();

  // Inside the inner view: the inner one wins.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 31);
  // Inside the outer view but outside the inner one.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 150.0, 150.0)), 30);
}

TEST(hit_test_respects_sibling_order) {
  Scene scene(400, 400);

  // Two overlapping siblings. GTK paints in child order, so the later one is on
  // top and should win a hit in the overlap.
  addChild(scene.root, 40, 0.0F, 0.0F, 100.0F, 100.0F);
  RnView *second = rn_view_new(41);
  g_object_ref_sink(second);
  rn_view_set_frame(second, 50.0F, 50.0F, 100.0F, 100.0F);
  rn_view_insert_child(scene.root, second, 1);
  scene.show();

  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 25.0, 25.0)), 40);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 125.0, 125.0)), 41);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 75.0, 75.0)), 41);
}

TEST(hit_test_follows_a_scroll_offset) {
  // The reason scrolling is expressed as an offset applied during allocation
  // rather than as a paint-time translation: children really do move, so GTK's
  // own picking follows them and hit testing needs no special case.
  Scene scene(400, 400);
  RnView *scroller = addChild(scene.root, 50, 0.0F, 0.0F, 200.0F, 200.0F);
  addChild(scroller, 51, 0.0F, 300.0F, 200.0F, 100.0F);
  scene.show();

  // The row sits below the viewport, so nothing of it is under this point.
  EXPECT(basalt::hitTestTag(scene.root, 100.0, 50.0) != 51);

  rn_view_set_scroll_offset(scroller, 0.0, 300.0);
  Scene::pump();

  // Scrolled into view, the same point now lands on it.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 100.0, 50.0)), 51);
}

TEST(hit_test_on_a_null_root_is_a_miss) {
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(nullptr, 0.0, 0.0)), 0);
}

// Hover walks the same widget tree from the same pick, and has to survive
// everything the touch path does: a point that hits nothing, a view with no
// emitter, and the cursor leaving the surface. No emitter is attached to
// anything here, so nothing is delivered -- which is also what a cursor moving
// during a surface teardown looks like from inside the dispatcher.
TEST(a_synthesised_hover_walks_the_tree_without_an_emitter) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(1);

  GtkWidget *window = gtk_window_new();
  gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
  gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(root));
  addChild(root, 30, 10.0F, 10.0F, 100.0F, 100.0F);
  gtk_widget_set_visible(window, TRUE);
  Scene::pump();

  basalt::GtkTouchDispatcher dispatcher(&manager, root);
  dispatcher.synthesiseHover(50.0, 50.0);
  dispatcher.synthesiseHover(300.0, 300.0);
  // Negative means the cursor left the surface.
  dispatcher.synthesiseHover(-1.0, -1.0);

  gtk_window_destroy(GTK_WINDOW(window));
  manager.destroySurfaceRoot(1);
}

// `pointerEvents`, which decides what a press can land on rather than what is
// drawn. GTK expresses exactly one of the four values -- `none`, through
// can-target -- so these are also the tests for the resolution
// GtkTouchDispatcher does for the other two.
TEST(pointer_events_none_passes_a_press_through_to_what_is_behind) {
  Scene scene(400, 400);
  RnView *behind = addChild(scene.root, 60, 0.0F, 0.0F, 200.0F, 200.0F);
  RnView *over = rn_view_new(61);
  g_object_ref_sink(over);
  rn_view_set_frame(over, 0.0F, 0.0F, 200.0F, 200.0F);
  rn_view_insert_child(scene.root, over, 1);
  scene.show();
  (void)behind;

  // Painted last, so it is on top and takes the press.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 61);

  rn_view_set_pointer_events(over, RN_POINTER_EVENTS_NONE);
  Scene::pump();
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 60);
}

TEST(pointer_events_none_takes_the_children_with_it) {
  Scene scene(400, 400);
  RnView *over = addChild(scene.root, 62, 0.0F, 0.0F, 200.0F, 200.0F);
  addChild(over, 63, 0.0F, 0.0F, 100.0F, 100.0F);
  scene.show();

  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 63);
  // The whole subtree leaves hit testing, not just the view the prop is on --
  // which is GTK's own can-target semantics and the reason `none` needs no
  // help from the dispatcher.
  rn_view_set_pointer_events(over, RN_POINTER_EVENTS_NONE);
  Scene::pump();
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 1);
}

TEST(pointer_events_box_none_is_transparent_and_its_children_are_not) {
  Scene scene(400, 400);
  addChild(scene.root, 64, 0.0F, 0.0F, 300.0F, 300.0F);
  RnView *overlay = rn_view_new(65);
  g_object_ref_sink(overlay);
  rn_view_set_frame(overlay, 0.0F, 0.0F, 300.0F, 300.0F);
  rn_view_insert_child(scene.root, overlay, 1);
  addChild(overlay, 66, 0.0F, 0.0F, 100.0F, 100.0F);
  rn_view_set_pointer_events(overlay, RN_POINTER_EVENTS_BOX_NONE);
  scene.show();

  // Over the child: the overlay's children are still targets.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 66);
  // Over the overlay and nothing inside it: the press belongs to the view
  // *behind*, not to the overlay's parent. Reaching that answer is what the
  // re-pick in pickTarget is for; returning the parent would look right until
  // something was underneath.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 200.0, 200.0)), 64);
  // And the view is targetable again afterwards -- the suppression lasts for
  // one pick, not for good.
  EXPECT(gtk_widget_get_can_target(GTK_WIDGET(overlay)));
}

TEST(pointer_events_box_only_swallows_presses_meant_for_its_children) {
  Scene scene(400, 400);
  RnView *panel = addChild(scene.root, 67, 0.0F, 0.0F, 300.0F, 300.0F);
  addChild(panel, 68, 0.0F, 0.0F, 100.0F, 100.0F);
  scene.show();

  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 68);
  rn_view_set_pointer_events(panel, RN_POINTER_EVENTS_BOX_ONLY);
  Scene::pump();
  // The press lands on the panel even though it is over the child, which is
  // what makes a disabled panel disable everything in it.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 50.0, 50.0)), 67);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 200.0, 200.0)), 67);
}

// `hitSlop`: how far outside its own box a view answers a press.
//
// The prop that exists because a 20pt icon in a 44pt row is what the eye aims
// at and the cursor misses. Asserted through the same hit test every press goes
// through, rather than against the stored insets: a view that remembered its
// slop and did not widen `contains` would leave every target the size it was.

TEST(hit_slop_grows_the_target_outside_the_box) {
  Scene scene(400, 400);
  RnView *icon = addChild(scene.root, 60, 100.0F, 100.0F, 20.0F, 20.0F);
  const float insets[4] = {12.0F, 12.0F, 12.0F, 12.0F};
  rn_view_set_hit_slop(icon, insets);
  scene.show();

  // Inside the box, as before.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 110.0, 110.0)), 60);
  // Ten points outside it on each side, which is inside the slop.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 92.0, 110.0)), 60);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 128.0, 110.0)), 60);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 110.0, 92.0)), 60);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 110.0, 128.0)), 60);
  // And past it, which is the root again.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 80.0, 110.0)), 1);
}

// Each edge on its own, because four numbers in one struct is four chances to
// read one into the wrong side: a slop that grew the top when the app asked for
// the bottom would pass any symmetric test.
TEST(hit_slop_applies_each_edge_where_it_was_asked_for) {
  Scene scene(400, 400);
  RnView *view = addChild(scene.root, 61, 100.0F, 100.0F, 20.0F, 20.0F);
  // Top only.
  const float top[4] = {15.0F, 0.0F, 0.0F, 0.0F};
  rn_view_set_hit_slop(view, top);
  scene.show();

  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 110.0, 90.0)), 61);
  // Not the other three.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 110.0, 130.0)), 1);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 90.0, 110.0)), 1);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 130.0, 110.0)), 1);

  // And the opposite edge, through the same view: the prop can change.
  const float bottom[4] = {0.0F, 0.0F, 15.0F, 0.0F};
  rn_view_set_hit_slop(view, bottom);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 110.0, 130.0)), 61);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 110.0, 90.0)), 1);
}

// Taken away again. A slop left behind is a view that swallows presses meant for
// its neighbour, which is harder to notice than a target that is too small.
TEST(hit_slop_can_be_taken_away_again) {
  Scene scene(400, 400);
  RnView *view = addChild(scene.root, 62, 100.0F, 100.0F, 20.0F, 20.0F);
  const float insets[4] = {12.0F, 12.0F, 12.0F, 12.0F};
  rn_view_set_hit_slop(view, insets);
  scene.show();
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 92.0, 110.0)), 62);

  rn_view_set_hit_slop(view, nullptr);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 92.0, 110.0)), 1);
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 110.0, 110.0)), 62);
}

// A slop that overlaps a sibling does not win over it: the sibling is drawn on
// top, and a target that reached under something visible would take presses
// meant for it.
TEST(hit_slop_does_not_beat_a_view_drawn_over_it) {
  Scene scene(400, 400);
  RnView *first = addChild(scene.root, 63, 100.0F, 100.0F, 20.0F, 20.0F);
  const float insets[4] = {0.0F, 40.0F, 0.0F, 0.0F};
  rn_view_set_hit_slop(first, insets);

  RnView *second = rn_view_new(64);
  g_object_ref_sink(second);
  rn_view_set_frame(second, 130.0F, 100.0F, 20.0F, 20.0F);
  rn_view_insert_child(scene.root, second, 1);
  scene.show();

  // Inside the slop and outside the sibling: the slop answers.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 125.0, 110.0)), 63);
  // Inside both: the sibling, which is on top.
  EXPECT_EQ(static_cast<int>(basalt::hitTestTag(scene.root, 140.0, 110.0)), 64);
}

// And it is in the tree dump, which is the only way to see it: a view with a
// bigger target is drawn exactly like one without.
TEST(hit_slop_is_reported_in_the_tree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 20.0F, 20.0F);

  char *text = rn_view_describe_tree(view);
  const std::string none(text);
  g_free(text);
  EXPECT(none.find("hit-slop=") == std::string::npos);

  const float insets[4] = {1.0F, 2.0F, 3.0F, 4.0F};
  rn_view_set_hit_slop(view, insets);
  text = rn_view_describe_tree(view);
  const std::string dumped(text);
  g_free(text);
  EXPECT(dumped.find("hit-slop=(1,2,3,4)") != std::string::npos);

  g_object_unref(view);
}
