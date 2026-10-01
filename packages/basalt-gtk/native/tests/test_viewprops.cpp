// Tests for the View props added last: borders, radii, transform, zIndex and
// display.
//
// The transform tests earn their place. React Native's matrix is CSS matrix3d
// order and graphene's is nominally row-major, and the two coincide in memory
// for translation but are transposes of each other for rotation. Asserting on
// where a corner actually lands is the only way to know which is happening.

#include "TestHarness.h"

#include "RnView.h"

#include <cmath>
#include <sstream>
#include <string>

namespace {

void layout(RnView *root, int width, int height) {
  gtk_widget_set_size_request(GTK_WIDGET(root), width, height);
  gtk_widget_allocate(GTK_WIDGET(root), width, height, -1, nullptr);
}

RnView *addChild(RnView *parent, int tag, float x, float y, float width, float height) {
  RnView *child = rn_view_new(tag);
  g_object_ref_sink(child);
  rn_view_set_frame(child, x, y, width, height);
  rn_view_insert_child(parent, child, 0);
  return child;
}

// Where a point inside the child ends up in the parent, after the transform.
bool mapPoint(RnView *child, RnView *parent, float x, float y, graphene_point_t *out) {
  const graphene_point_t point = {x, y};
  return gtk_widget_compute_point(GTK_WIDGET(child), GTK_WIDGET(parent), &point, out);
}

// Half a turn about Y, which mirrors the widget: a determinant of -1, and so
// a back face pointed away from the viewer.
const float kFlippedAboutY[16] = {-1, 0, 0, 0, 0, 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1};

} // namespace

TEST(transform_translate_moves_the_widget) {
  RnView *root = rn_view_new(1);
  g_object_ref_sink(root);
  RnView *child = addChild(root, 10, 0.0F, 0.0F, 100.0F, 100.0F);

  graphene_matrix_t matrix;
  const graphene_point3d_t offset = {50.0F, 20.0F, 0.0F};
  graphene_matrix_init_translate(&matrix, &offset);
  rn_view_set_transform(child, &matrix);
  layout(root, 400, 400);

  graphene_point_t mapped;
  EXPECT(mapPoint(child, root, 0.0F, 0.0F, &mapped));
  EXPECT_NEAR(mapped.x, 50.0, 0.5);
  EXPECT_NEAR(mapped.y, 20.0, 0.5);

  g_object_unref(root);
}

TEST(transform_is_anchored_on_the_centre) {
  // Every other React Native platform anchors a transform on the view's centre,
  // and transformOrigin is measured from there. A scale about the top-left
  // would leave the origin where it is; about the centre it moves.
  RnView *root = rn_view_new(1);
  g_object_ref_sink(root);
  RnView *child = addChild(root, 11, 0.0F, 0.0F, 100.0F, 100.0F);

  graphene_matrix_t matrix;
  graphene_matrix_init_scale(&matrix, 2.0F, 2.0F, 1.0F);
  rn_view_set_transform(child, &matrix);
  layout(root, 400, 400);

  graphene_point_t mapped;
  EXPECT(mapPoint(child, root, 0.0F, 0.0F, &mapped));
  // Doubling about the centre (50,50) sends the top-left corner to (-50,-50).
  EXPECT_NEAR(mapped.x, -50.0, 0.5);
  EXPECT_NEAR(mapped.y, -50.0, 0.5);

  // ...and the centre stays put, which is what "anchored" means.
  EXPECT(mapPoint(child, root, 50.0F, 50.0F, &mapped));
  EXPECT_NEAR(mapped.x, 50.0, 0.5);
  EXPECT_NEAR(mapped.y, 50.0, 0.5);

  g_object_unref(root);
}

TEST(transform_follows_into_hit_testing) {
  // The reason a transform is composed during allocation rather than at paint
  // time: a transformed view has to be picked where it looks, not where its
  // frame says it is.
  RnView *root = rn_view_new(1);
  g_object_ref_sink(root);
  RnView *child = addChild(root, 12, 0.0F, 0.0F, 100.0F, 100.0F);

  graphene_matrix_t matrix;
  const graphene_point3d_t offset = {200.0F, 0.0F, 0.0F};
  graphene_matrix_init_translate(&matrix, &offset);
  rn_view_set_transform(child, &matrix);
  layout(root, 400, 400);

  graphene_rect_t bounds;
  EXPECT(gtk_widget_compute_bounds(GTK_WIDGET(child), GTK_WIDGET(root), &bounds));
  EXPECT_NEAR(bounds.origin.x, 200.0, 0.5);

  g_object_unref(root);
}

TEST(clearing_a_transform_restores_the_frame) {
  RnView *root = rn_view_new(1);
  g_object_ref_sink(root);
  RnView *child = addChild(root, 13, 10.0F, 10.0F, 50.0F, 50.0F);

  graphene_matrix_t matrix;
  const graphene_point3d_t offset = {100.0F, 0.0F, 0.0F};
  graphene_matrix_init_translate(&matrix, &offset);
  rn_view_set_transform(child, &matrix);
  layout(root, 400, 400);

  rn_view_set_transform(child, nullptr);
  layout(root, 400, 400);

  graphene_rect_t bounds;
  EXPECT(gtk_widget_compute_bounds(GTK_WIDGET(child), GTK_WIDGET(root), &bounds));
  EXPECT_NEAR(bounds.origin.x, 10.0, 0.5);

  g_object_unref(root);
}

TEST(an_identity_transform_costs_nothing) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);

  graphene_matrix_t identity;
  graphene_matrix_init_identity(&identity);
  rn_view_set_transform(view, &identity);

  // Not just an optimisation: an identity matrix must not make the view behave
  // as though it were transformed.
  char *description = rn_view_describe_tree(view);
  EXPECT(description != nullptr);
  g_free(description);

  g_object_unref(view);
}

TEST(display_none_takes_a_view_out_of_layout) {
  RnView *root = rn_view_new(1);
  g_object_ref_sink(root);
  RnView *child = addChild(root, 14, 0.0F, 0.0F, 100.0F, 100.0F);

  rn_view_set_hidden(child, TRUE);
  // RnLayout skips a child that should not be laid out, so an invisible view is
  // neither placed nor painted.
  EXPECT(!gtk_widget_should_layout(GTK_WIDGET(child)));

  layout(root, 400, 400);
  g_object_unref(root);
}


// A back face turned away from the viewer is hidden, and says so in the tree
// -- without which the only prop here that removes a view entirely is the one
// the dump cannot show, and scripts/compare_hosts.sh compares two views that
// look identical and are not.
TEST(a_back_face_turned_away_is_hidden) {
  RnView *view = rn_view_new(22);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 64.0F, 64.0F);
  rn_view_set_hides_back_face(view, TRUE);

  graphene_matrix_t flipped;
  graphene_matrix_init_from_float(&flipped, kFlippedAboutY);
  rn_view_set_transform(view, &flipped);
  EXPECT(!gtk_widget_get_visible(GTK_WIDGET(view)));

  char *description = rn_view_describe_tree(view);
  EXPECT(std::string(description).find(" hidden") != std::string::npos);
  g_free(description);

  // Turning the prop off shows it again. The early return this used to have
  // left the widget hidden for good.
  rn_view_set_hides_back_face(view, FALSE);
  EXPECT(gtk_widget_get_visible(GTK_WIDGET(view)));

  g_object_unref(view);
}

// The two reasons a view can be hidden are independent, and Fabric applies
// them through different calls: props first, then layout metrics. So a card
// turned away from the viewer had `display: none`'s "no" written over the top
// of it on the very same mount, and came back.
TEST(display_none_and_a_back_face_do_not_cancel_each_other) {
  RnView *view = rn_view_new(22);
  g_object_ref_sink(view);
  rn_view_set_hides_back_face(view, TRUE);

  graphene_matrix_t flipped;
  graphene_matrix_init_from_float(&flipped, kFlippedAboutY);
  rn_view_set_transform(view, &flipped);

  // What GtkMountingManager says about every view that is not display: none.
  rn_view_set_hidden(view, FALSE);
  EXPECT(!gtk_widget_get_visible(GTK_WIDGET(view)));

  // And the other way: facing the viewer must not reveal what the app hid.
  rn_view_set_hidden(view, TRUE);
  rn_view_set_transform(view, nullptr);
  EXPECT(!gtk_widget_get_visible(GTK_WIDGET(view)));

  g_object_unref(view);
}

TEST(borders_and_radii_are_accepted_and_described) {
  RnView *view = rn_view_new(20);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);

  const graphene_size_t radii[4] = {{8.0F, 8.0F}, {8.0F, 8.0F}, {0.0F, 0.0F}, {0.0F, 0.0F}};
  rn_view_set_border_radii(view, radii);

  const float widths[4] = {2.0F, 2.0F, 2.0F, 2.0F};
  const GdkRGBA colors[4] = {
      {1.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F, 1.0F},
      {1.0F, 0.0F, 0.0F, 1.0F}, {1.0F, 0.0F, 0.0F, 1.0F}};
  rn_view_set_borders(view, widths, colors);

  // Clearing must be possible too; a view that loses its border should lose it.
  rn_view_set_border_radii(view, nullptr);
  rn_view_set_borders(view, nullptr, nullptr);

  g_object_unref(view);
}

TEST(z_index_reorders_painting_not_the_child_list) {
  RnView *root = rn_view_new(1);
  g_object_ref_sink(root);
  RnView *first = addChild(root, 30, 0.0F, 0.0F, 10.0F, 10.0F);
  RnView *second = rn_view_new(31);
  g_object_ref_sink(second);
  rn_view_insert_child(root, second, 1);

  rn_view_set_z_index(first, 10);
  rn_view_set_z_index(second, 1);

  // Painting order changes, but Fabric indexes into the child list on every
  // Insert and Remove, so that list must not be resorted.
  EXPECT_EQ(rn_view_get_tag(RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(root)))), 30);
  EXPECT_EQ(rn_view_get_tag(RN_VIEW(gtk_widget_get_last_child(GTK_WIDGET(root)))), 31);

  g_object_unref(root);
}
