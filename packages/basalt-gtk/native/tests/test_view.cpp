// Tests for the widget layer: frames, child ordering, scrolling and clipping.
//
// RnView has no React Native dependency, so these need no Fabric machinery.

#include "TestHarness.h"

#include "RnView.h"

#include <sstream>

namespace {

// Allocating a widget tree the way GTK would, so the layout manager runs.
void layout(RnView *root, int width, int height) {
  gtk_widget_set_size_request(GTK_WIDGET(root), width, height);
  gtk_widget_allocate(GTK_WIDGET(root), width, height, -1, nullptr);
}

int childCount(RnView *view) {
  int count = 0;
  for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(view)); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    ++count;
  }
  return count;
}

} // namespace

TEST(view_stores_its_tag_and_frame) {
  RnView *view = rn_view_new(7);
  g_object_ref_sink(view);

  EXPECT_EQ(rn_view_get_tag(view), 7);

  rn_view_set_frame(view, 1.5F, 2.5F, 30.0F, 40.0F);
  graphene_rect_t frame;
  rn_view_get_frame(view, &frame);
  EXPECT_NEAR(frame.origin.x, 1.5, 0.001);
  EXPECT_NEAR(frame.origin.y, 2.5, 0.001);
  EXPECT_NEAR(frame.size.width, 30.0, 0.001);
  EXPECT_NEAR(frame.size.height, 40.0, 0.001);

  g_object_unref(view);
}

TEST(children_insert_at_the_requested_index) {
  RnView *parent = rn_view_new(1);
  g_object_ref_sink(parent);

  RnView *a = rn_view_new(10);
  RnView *b = rn_view_new(11);
  RnView *c = rn_view_new(12);
  g_object_ref_sink(a);
  g_object_ref_sink(b);
  g_object_ref_sink(c);

  rn_view_insert_child(parent, a, 0);
  rn_view_insert_child(parent, b, 1);
  rn_view_insert_child(parent, c, 1);

  EXPECT_EQ(childCount(parent), 3);
  EXPECT_EQ(rn_view_get_tag(RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(parent)))), 10);
  EXPECT_EQ(rn_view_get_tag(RN_VIEW(gtk_widget_get_next_sibling(
               gtk_widget_get_first_child(GTK_WIDGET(parent))))),
           12);

  rn_view_remove_child(parent, c);
  EXPECT_EQ(childCount(parent), 2);

  g_object_unref(c);
  g_object_unref(parent);
}

TEST(layout_places_children_at_their_frames) {
  RnView *parent = rn_view_new(1);
  g_object_ref_sink(parent);

  RnView *child = rn_view_new(2);
  g_object_ref_sink(child);
  rn_view_set_frame(child, 20.0F, 30.0F, 40.0F, 50.0F);
  rn_view_insert_child(parent, child, 0);

  layout(parent, 200, 200);

  graphene_rect_t bounds;
  EXPECT(gtk_widget_compute_bounds(GTK_WIDGET(child), GTK_WIDGET(parent), &bounds));
  EXPECT_NEAR(bounds.origin.x, 20.0, 0.5);
  EXPECT_NEAR(bounds.origin.y, 30.0, 0.5);
  EXPECT_NEAR(bounds.size.width, 40.0, 0.5);
  EXPECT_NEAR(bounds.size.height, 50.0, 0.5);

  g_object_unref(parent);
}

TEST(scroll_offset_shifts_children) {
  RnView *scroller = rn_view_new(1);
  g_object_ref_sink(scroller);

  RnView *content = rn_view_new(2);
  g_object_ref_sink(content);
  rn_view_set_frame(content, 0.0F, 0.0F, 100.0F, 1000.0F);
  rn_view_insert_child(scroller, content, 0);

  layout(scroller, 100, 200);
  graphene_rect_t before;
  EXPECT(gtk_widget_compute_bounds(GTK_WIDGET(content), GTK_WIDGET(scroller), &before));
  EXPECT_NEAR(before.origin.y, 0.0, 0.5);

  // Scrolling moves the children, not the scroller's own frame. This is what
  // makes gtk_widget_pick follow the scroll for free.
  rn_view_set_scroll_offset(scroller, 0.0, 150.0);
  layout(scroller, 100, 200);

  graphene_rect_t after;
  EXPECT(gtk_widget_compute_bounds(GTK_WIDGET(content), GTK_WIDGET(scroller), &after));
  EXPECT_NEAR(after.origin.y, -150.0, 0.5);

  double x = 0;
  double y = 0;
  rn_view_get_scroll_offset(scroller, &x, &y);
  EXPECT_NEAR(x, 0.0, 0.001);
  EXPECT_NEAR(y, 150.0, 0.001);

  g_object_unref(scroller);
}

TEST(measure_reports_zero_so_gtk_never_second_guesses_yoga) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 123.0F, 456.0F);

  int minimum = -1;
  int natural = -1;
  int minimumBaseline = 0;
  int naturalBaseline = 0;
  gtk_widget_measure(
      GTK_WIDGET(view), GTK_ORIENTATION_HORIZONTAL, -1, &minimum, &natural, &minimumBaseline, &naturalBaseline);

  // React Native is the only source of truth for size. If this ever reports the
  // frame instead, GTK starts participating in layout and the two disagree.
  EXPECT_EQ(minimum, 0);
  EXPECT_EQ(natural, 0);

  g_object_unref(view);
}

TEST(dispose_unparents_children_rather_than_leaking_them) {
  RnView *parent = rn_view_new(1);
  g_object_ref_sink(parent);

  RnView *child = rn_view_new(2);
  g_object_ref_sink(child);
  rn_view_insert_child(parent, child, 0);

  // A Delete can arrive with children still attached. GTK warns and leaks if a
  // widget is finalised while it still has any, so dispose unparents them.
  g_object_unref(parent);

  EXPECT(gtk_widget_get_parent(GTK_WIDGET(child)) == nullptr);
  g_object_unref(child);
}
