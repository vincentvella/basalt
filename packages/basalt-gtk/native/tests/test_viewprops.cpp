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

// The render node a widget paints, so a test can assert what is in it rather
// than only what the widget was told. `gtk_widget_allocate` above is what makes
// this possible without a window: an unallocated widget snapshots to nothing.
//
// The class vfunc is called directly rather than through
// `gtk_widget_snapshot_child`, which wants a realized parent.
GskRenderNode *paintedNode(RnView *view) {
  GtkSnapshot *snapshot = gtk_snapshot_new();
  GTK_WIDGET_GET_CLASS(GTK_WIDGET(view))->snapshot(GTK_WIDGET(view), snapshot);
  return gtk_snapshot_free_to_node(snapshot);
}

// How many clip nodes are anywhere in that tree. Counted rather than located,
// because where GSK puts one inside a container is its business and an assertion
// on the shape of the tree would break on a GTK upgrade that said the same thing
// differently.
int clipNodesIn(GskRenderNode *node) {
  if (node == nullptr) {
    return 0;
  }
  const GskRenderNodeType type = gsk_render_node_get_node_type(node);
  switch (type) {
    case GSK_CLIP_NODE:
      return 1 + clipNodesIn(gsk_clip_node_get_child(node));
    case GSK_ROUNDED_CLIP_NODE:
      return 1 + clipNodesIn(gsk_rounded_clip_node_get_child(node));
    case GSK_CONTAINER_NODE: {
      int found = 0;
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        found += clipNodesIn(gsk_container_node_get_child(node, i));
      }
      return found;
    }
    case GSK_TRANSFORM_NODE:
      return clipNodesIn(gsk_transform_node_get_child(node));
    case GSK_BLUR_NODE:
      return clipNodesIn(gsk_blur_node_get_child(node));
    case GSK_OPACITY_NODE:
      return clipNodesIn(gsk_opacity_node_get_child(node));
    default:
      return 0;
  }
}

// Blur nodes anywhere in the tree, counted the same way and for the same reason
// as the clip ones: where GSK nests one is its business.
int blurNodesIn(GskRenderNode *node) {
  if (node == nullptr) {
    return 0;
  }
  switch (gsk_render_node_get_node_type(node)) {
    case GSK_BLUR_NODE:
      return 1 + blurNodesIn(gsk_blur_node_get_child(node));
    case GSK_CLIP_NODE:
      return blurNodesIn(gsk_clip_node_get_child(node));
    case GSK_ROUNDED_CLIP_NODE:
      return blurNodesIn(gsk_rounded_clip_node_get_child(node));
    case GSK_CONTAINER_NODE: {
      int found = 0;
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        found += blurNodesIn(gsk_container_node_get_child(node, i));
      }
      return found;
    }
    case GSK_TRANSFORM_NODE:
      return blurNodesIn(gsk_transform_node_get_child(node));
    case GSK_OPACITY_NODE:
      return blurNodesIn(gsk_opacity_node_get_child(node));
    default:
      return 0;
  }
}

// Which of the two border nodes a view painted. A solid border is GTK's border
// node and a dotted or dashed one is a stroked path, so the pair says both that
// the style took effect and that the other kind was not drawn as well.
struct BorderNodes {
  int borders;
  int strokes;
};

void countBorderNodes(GskRenderNode *node, BorderNodes *found) {
  if (node == nullptr) {
    return;
  }
  switch (gsk_render_node_get_node_type(node)) {
    case GSK_BORDER_NODE:
      found->borders++;
      return;
    case GSK_STROKE_NODE:
      found->strokes++;
      return;
    case GSK_CONTAINER_NODE:
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        countBorderNodes(gsk_container_node_get_child(node, i), found);
      }
      return;
    case GSK_CLIP_NODE:
      countBorderNodes(gsk_clip_node_get_child(node), found);
      return;
    case GSK_ROUNDED_CLIP_NODE:
      countBorderNodes(gsk_rounded_clip_node_get_child(node), found);
      return;
    case GSK_TRANSFORM_NODE:
      countBorderNodes(gsk_transform_node_get_child(node), found);
      return;
    case GSK_OPACITY_NODE:
      countBorderNodes(gsk_opacity_node_get_child(node), found);
      return;
    default:
      return;
  }
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

// The clip node that hides the lines `numberOfLines` with `ellipsizeMode: 'clip'`
// cut. Pango leaves those lines in the layout, so the measurement reports a
// shorter box and the widget has to stop painting at the same height; without
// the clip the hidden lines are still drawn and spill past the view. Nothing
// covered this, so sabotaging the clip was caught by no test at all. See
// backlog/text.md.

TEST(a_clipped_paragraph_paints_through_a_clip_node) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 200.0F);
  layout(view, 200, 200);

  // A paragraph of several lines limited to two, with no ellipsis, which is the
  // one combination Pango does not truncate itself.
  PangoContext *context = gtk_widget_create_pango_context(GTK_WIDGET(view));
  PangoLayout *text = pango_layout_new(context);
  pango_layout_set_text(text, "one two three four five six seven eight nine ten", -1);
  pango_layout_set_width(text, 60 * PANGO_SCALE);
  pango_layout_set_wrap(text, PANGO_WRAP_WORD_CHAR);
  pango_layout_set_ellipsize(text, PANGO_ELLIPSIZE_NONE);
  pango_layout_set_height(text, -2);

  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_text_layout(view, text, &black);

  GskRenderNode *painted = paintedNode(view);
  EXPECT(painted != nullptr);
  const int clipped = clipNodesIn(painted);

  // And the same paragraph with no limit, which must not be clipped.
  pango_layout_set_height(text, 0);
  rn_view_set_text_layout(view, text, &black);
  GskRenderNode *whole = paintedNode(view);
  const int unclipped = clipNodesIn(whole);

  EXPECT(clipped > unclipped);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  if (whole != nullptr) {
    gsk_render_node_unref(whole);
  }
  g_object_unref(text);
  g_object_unref(context);
  g_object_unref(view);
}

// `blurRadius` on an <Image>. GSK has a blur node, so what the prop does is
// directly observable in the render tree rather than only on screen, which is
// the difference between a test that holds and one that looks like it does.
//
// The blur wraps the image alone and not the view: a blurred photograph behind
// sharp text is the usual reason to ask for it.

TEST(a_blur_radius_puts_a_blur_node_around_the_image) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);
  layout(view, 100, 100);

  // A one-pixel texture is enough: what is asserted is the node around it.
  GBytes *pixel = g_bytes_new_static("\xff\x00\x00\xff", 4);
  GdkTexture *texture = gdk_memory_texture_new(1, 1, GDK_MEMORY_R8G8B8A8, pixel, 4);
  rn_view_set_texture(view, texture, RN_IMAGE_FIT_STRETCH);

  GskRenderNode *sharp = paintedNode(view);
  EXPECT(sharp != nullptr);
  EXPECT_EQ(blurNodesIn(sharp), 0);

  rn_view_set_image_blur(view, 8.0F);
  GskRenderNode *blurred = paintedNode(view);
  EXPECT(blurred != nullptr);
  EXPECT_EQ(blurNodesIn(blurred), 1);

  // And back to nothing, because a prop that stops being set has to stop
  // applying: an image whose blurRadius returns to zero is a sharp image.
  rn_view_set_image_blur(view, 0.0F);
  GskRenderNode *again = paintedNode(view);
  EXPECT_EQ(blurNodesIn(again), 0);

  if (sharp != nullptr) {
    gsk_render_node_unref(sharp);
  }
  if (blurred != nullptr) {
    gsk_render_node_unref(blurred);
  }
  if (again != nullptr) {
    gsk_render_node_unref(again);
  }
  g_object_unref(texture);
  g_bytes_unref(pixel);
  g_object_unref(view);
}

// And it is in the tree dump, which is what makes the *wiring* testable: the
// prop is read in a branch of GtkMountingManager that knows ImageProps, and
// every test above pushes the blur onto the widget by hand, so all of them pass
// while the mounting manager drops it. It did, until this line existed.
TEST(a_blur_radius_is_reported_in_the_tree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);

  GBytes *pixel = g_bytes_new_static("\xff\x00\x00\xff", 4);
  GdkTexture *texture = gdk_memory_texture_new(1, 1, GDK_MEMORY_R8G8B8A8, pixel, 4);
  rn_view_set_texture(view, texture, RN_IMAGE_FIT_STRETCH);

  rn_view_set_image_blur(view, 8.0F);
  char *dump = rn_view_describe_tree(view);
  const std::string blurred(dump);
  g_free(dump);
  EXPECT(blurred.find("blur=8") != std::string::npos);

  rn_view_set_image_blur(view, 0.0F);
  dump = rn_view_describe_tree(view);
  const std::string sharp(dump);
  g_free(dump);
  EXPECT(sharp.find("blur=") == std::string::npos);

  g_object_unref(texture);
  g_bytes_unref(pixel);
  g_object_unref(view);
}

TEST(a_negative_blur_radius_is_no_blur_rather_than_a_crash) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);
  layout(view, 100, 100);

  GBytes *pixel = g_bytes_new_static("\xff\x00\x00\xff", 4);
  GdkTexture *texture = gdk_memory_texture_new(1, 1, GDK_MEMORY_R8G8B8A8, pixel, 4);
  rn_view_set_texture(view, texture, RN_IMAGE_FIT_STRETCH);

  // Nothing stops an app sending one, and GSK would take it.
  rn_view_set_image_blur(view, -4.0F);
  GskRenderNode *painted = paintedNode(view);
  EXPECT_EQ(blurNodesIn(painted), 0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(texture);
  g_bytes_unref(pixel);
  g_object_unref(view);
}

// `borderStyle`. GTK's border node paints solid only, so dotted and dashed are
// a stroked path instead, and which node a view painted is visible in the render
// tree. That is what these assert: not that a style was stored, but that the
// drawing changed and that the solid border was not drawn as well.

TEST(a_solid_border_is_a_border_node) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);
  layout(view, 100, 100);

  const float widths[4] = {4.0F, 4.0F, 4.0F, 4.0F};
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  const GdkRGBA colors[4] = {black, black, black, black};
  rn_view_set_borders(view, widths, colors);

  GskRenderNode *painted = paintedNode(view);
  BorderNodes found{0, 0};
  countBorderNodes(painted, &found);
  EXPECT_EQ(found.borders, 1);
  EXPECT_EQ(found.strokes, 0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

TEST(a_dashed_border_is_a_stroke_instead) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);
  layout(view, 100, 100);

  const float widths[4] = {4.0F, 4.0F, 4.0F, 4.0F};
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  const GdkRGBA colors[4] = {black, black, black, black};
  rn_view_set_borders(view, widths, colors);
  rn_view_set_border_style(view, RN_BORDER_DASHED);

  GskRenderNode *painted = paintedNode(view);
  BorderNodes found{0, 0};
  countBorderNodes(painted, &found);
  // The stroke replaces the border rather than joining it: two would paint the
  // outline twice and the dashes would sit on a solid line.
  EXPECT_EQ(found.strokes, 1);
  EXPECT_EQ(found.borders, 0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

TEST(a_dotted_border_is_also_a_stroke) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);
  layout(view, 100, 100);

  const float widths[4] = {2.0F, 2.0F, 2.0F, 2.0F};
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  const GdkRGBA colors[4] = {black, black, black, black};
  rn_view_set_borders(view, widths, colors);
  rn_view_set_border_style(view, RN_BORDER_DOTTED);

  GskRenderNode *painted = paintedNode(view);
  BorderNodes found{0, 0};
  countBorderNodes(painted, &found);
  EXPECT_EQ(found.strokes, 1);
  EXPECT_EQ(found.borders, 0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// And the style is in the tree dump, which is the other half: these tests set it
// on the widget by hand, so they all passed while the mounting manager read the
// wrong field and no React app ever got a dashed border. The dump is what an
// end-to-end run can read.
TEST(a_border_style_is_reported_in_the_tree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);

  const float widths[4] = {3.0F, 3.0F, 3.0F, 3.0F};
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  const GdkRGBA colors[4] = {black, black, black, black};
  rn_view_set_borders(view, widths, colors);

  char *text = rn_view_describe_tree(view);
  const std::string solid(text);
  g_free(text);
  EXPECT(solid.find("border-style=") == std::string::npos);

  rn_view_set_border_style(view, RN_BORDER_DASHED);
  text = rn_view_describe_tree(view);
  const std::string dashed(text);
  g_free(text);
  EXPECT(dashed.find("border-style=dashed") != std::string::npos);

  rn_view_set_border_style(view, RN_BORDER_DOTTED);
  text = rn_view_describe_tree(view);
  const std::string dotted(text);
  g_free(text);
  EXPECT(dotted.find("border-style=dotted") != std::string::npos);

  g_object_unref(view);
}

TEST(a_border_style_on_a_view_with_no_border_draws_nothing) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 100.0F);
  layout(view, 100, 100);

  // A style without a width is not a border. Stroking here would put a line
  // around every view that mentioned borderStyle and set no borderWidth.
  rn_view_set_border_style(view, RN_BORDER_DASHED);

  GskRenderNode *painted = paintedNode(view);
  BorderNodes found{0, 0};
  countBorderNodes(painted, &found);
  EXPECT_EQ(found.strokes, 0);
  EXPECT_EQ(found.borders, 0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}
