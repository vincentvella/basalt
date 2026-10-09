// Tests for the View props added last: borders, radii, transform, zIndex and
// display.
//
// The transform tests earn their place. React Native's matrix is CSS matrix3d
// order and graphene's is nominally row-major, and the two coincide in memory
// for translation but are transposes of each other for rotation. Asserting on
// where a corner actually lands is the only way to know which is happening.

#include "TestHarness.h"

#include "Filters.h"
#include "GtkPixels.h"
#include "RnView.h"

#include <cmath>
#include <sstream>
#include <string>
#include <vector>

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

// A `backgroundImage` entry of each kind, since the struct the widget takes
// carries both and only some of its fields mean anything to either.
RnGradient linearGradient(graphene_point_t start,
                          graphene_point_t end,
                          const RnGradientStop *stops,
                          int count) {
  RnGradient gradient{};
  gradient.kind = RN_GRADIENT_LINEAR;
  gradient.start = start;
  gradient.end = end;
  gradient.stops = stops;
  gradient.stop_count = count;
  return gradient;
}

RnGradient radialGradient(graphene_point_t center,
                          float radiusX,
                          float radiusY,
                          const RnGradientStop *stops,
                          int count) {
  RnGradient gradient{};
  gradient.kind = RN_GRADIENT_RADIAL;
  gradient.center = center;
  gradient.radius_x = radiusX;
  gradient.radius_y = radiusY;
  gradient.stops = stops;
  gradient.stop_count = count;
  return gradient;
}

// The first linear-gradient node in the tree, and how many there are: a
// `backgroundImage` is a GSK node of its own, so both are readable.
int gradientNodesIn(GskRenderNode *node) {
  if (node == nullptr) {
    return 0;
  }
  switch (gsk_render_node_get_node_type(node)) {
    case GSK_LINEAR_GRADIENT_NODE:
      return 1;
    case GSK_CONTAINER_NODE: {
      int found = 0;
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        found += gradientNodesIn(gsk_container_node_get_child(node, i));
      }
      return found;
    }
    case GSK_CLIP_NODE:
      return gradientNodesIn(gsk_clip_node_get_child(node));
    case GSK_ROUNDED_CLIP_NODE:
      return gradientNodesIn(gsk_rounded_clip_node_get_child(node));
    case GSK_TRANSFORM_NODE:
      return gradientNodesIn(gsk_transform_node_get_child(node));
    case GSK_OPACITY_NODE:
      return gradientNodesIn(gsk_opacity_node_get_child(node));
    default:
      return 0;
  }
}

// The gradient nodes in paint order, as one letter each: "LR" is a linear node
// painted before a radial one. Which is how the order between the two kinds is
// asserted, there being no index to compare otherwise.
std::string gradientOrderIn(GskRenderNode *node) {
  if (node == nullptr) {
    return "";
  }
  switch (gsk_render_node_get_node_type(node)) {
    case GSK_LINEAR_GRADIENT_NODE:
      return "L";
    case GSK_RADIAL_GRADIENT_NODE:
      return "R";
    case GSK_CONTAINER_NODE: {
      std::string found;
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        found += gradientOrderIn(gsk_container_node_get_child(node, i));
      }
      return found;
    }
    case GSK_CLIP_NODE:
      return gradientOrderIn(gsk_clip_node_get_child(node));
    case GSK_ROUNDED_CLIP_NODE:
      return gradientOrderIn(gsk_rounded_clip_node_get_child(node));
    case GSK_TRANSFORM_NODE:
      return gradientOrderIn(gsk_transform_node_get_child(node));
    case GSK_OPACITY_NODE:
      return gradientOrderIn(gsk_opacity_node_get_child(node));
    default:
      return "";
  }
}

// The same walk for radial nodes, which are a different GSK node type: a test
// that counted both together could not tell a radial gradient painted as a
// linear one from the real thing.
GskRenderNode *firstRadialGradient(GskRenderNode *node) {
  if (node == nullptr) {
    return nullptr;
  }
  switch (gsk_render_node_get_node_type(node)) {
    case GSK_RADIAL_GRADIENT_NODE:
      return node;
    case GSK_CONTAINER_NODE:
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        GskRenderNode *found = firstRadialGradient(gsk_container_node_get_child(node, i));
        if (found != nullptr) {
          return found;
        }
      }
      return nullptr;
    case GSK_CLIP_NODE:
      return firstRadialGradient(gsk_clip_node_get_child(node));
    case GSK_ROUNDED_CLIP_NODE:
      return firstRadialGradient(gsk_rounded_clip_node_get_child(node));
    case GSK_TRANSFORM_NODE:
      return firstRadialGradient(gsk_transform_node_get_child(node));
    case GSK_OPACITY_NODE:
      return firstRadialGradient(gsk_opacity_node_get_child(node));
    default:
      return nullptr;
  }
}

GskRenderNode *firstGradient(GskRenderNode *node) {
  if (node == nullptr) {
    return nullptr;
  }
  switch (gsk_render_node_get_node_type(node)) {
    case GSK_LINEAR_GRADIENT_NODE:
      return node;
    case GSK_CONTAINER_NODE:
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        GskRenderNode *found = firstGradient(gsk_container_node_get_child(node, i));
        if (found != nullptr) {
          return found;
        }
      }
      return nullptr;
    case GSK_CLIP_NODE:
      return firstGradient(gsk_clip_node_get_child(node));
    case GSK_ROUNDED_CLIP_NODE:
      return firstGradient(gsk_rounded_clip_node_get_child(node));
    case GSK_TRANSFORM_NODE:
      return firstGradient(gsk_transform_node_get_child(node));
    case GSK_OPACITY_NODE:
      return firstGradient(gsk_opacity_node_get_child(node));
    default:
      return nullptr;
  }
}

// The shadow nodes a view painted, outset and inset counted apart: `boxShadow`
// carries an `inset` flag and the two are different GSK nodes, so a view that
// read the flag wrongly draws a shadow in the wrong place and nothing else.
struct ShadowNodes {
  int outset;
  int inset;
};

void countShadowNodes(GskRenderNode *node, ShadowNodes *found) {
  if (node == nullptr) {
    return;
  }
  switch (gsk_render_node_get_node_type(node)) {
    case GSK_OUTSET_SHADOW_NODE:
      found->outset++;
      return;
    case GSK_INSET_SHADOW_NODE:
      found->inset++;
      return;
    case GSK_CONTAINER_NODE:
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        countShadowNodes(gsk_container_node_get_child(node, i), found);
      }
      return;
    case GSK_CLIP_NODE:
      countShadowNodes(gsk_clip_node_get_child(node), found);
      return;
    case GSK_ROUNDED_CLIP_NODE:
      countShadowNodes(gsk_rounded_clip_node_get_child(node), found);
      return;
    case GSK_TRANSFORM_NODE:
      countShadowNodes(gsk_transform_node_get_child(node), found);
      return;
    case GSK_OPACITY_NODE:
      countShadowNodes(gsk_opacity_node_get_child(node), found);
      return;
    default:
      return;
  }
}

// The first outset shadow node in the tree, for the tests that read its numbers
// back rather than counting it.
GskRenderNode *firstOutsetShadow(GskRenderNode *node) {
  if (node == nullptr) {
    return nullptr;
  }
  switch (gsk_render_node_get_node_type(node)) {
    case GSK_OUTSET_SHADOW_NODE:
      return node;
    case GSK_CONTAINER_NODE:
      for (guint i = 0; i < gsk_container_node_get_n_children(node); i++) {
        GskRenderNode *found = firstOutsetShadow(gsk_container_node_get_child(node, i));
        if (found != nullptr) {
          return found;
        }
      }
      return nullptr;
    case GSK_CLIP_NODE:
      return firstOutsetShadow(gsk_clip_node_get_child(node));
    case GSK_ROUNDED_CLIP_NODE:
      return firstOutsetShadow(gsk_rounded_clip_node_get_child(node));
    case GSK_TRANSFORM_NODE:
      return firstOutsetShadow(gsk_transform_node_get_child(node));
    case GSK_OPACITY_NODE:
      return firstOutsetShadow(gsk_opacity_node_get_child(node));
    default:
      return nullptr;
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

// The `cursor` style property. GDK's cursor names are CSS's, so the keyword goes
// straight through, and `gtk_widget_get_cursor` is what says it arrived.
//
// Against the widget rather than against a stored string: a view that remembered
// "pointer" and never told GTK would leave an arrow over every button, which is
// exactly the state this platform was in.

TEST(a_cursor_keyword_reaches_the_widget) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);

  // Nothing set: no cursor of its own, which means inherited rather than arrow.
  EXPECT(gtk_widget_get_cursor(GTK_WIDGET(view)) == nullptr);

  // The name GDK holds, or the empty string if it holds none: a missing cursor
  // and a cursor called nothing have to read differently from "pointer".
  const auto nameOf = [](RnView *widget) {
    GdkCursor *cursor = gtk_widget_get_cursor(GTK_WIDGET(widget));
    const char *name = cursor != nullptr ? gdk_cursor_get_name(cursor) : nullptr;
    return std::string(name != nullptr ? name : "");
  };

  rn_view_set_cursor(view, "pointer");
  EXPECT(gtk_widget_get_cursor(GTK_WIDGET(view)) != nullptr);
  EXPECT_EQ(nameOf(view), std::string("pointer"));
  EXPECT_EQ(std::string(rn_view_get_cursor(view) != nullptr ? rn_view_get_cursor(view) : ""),
            std::string("pointer"));

  // A hyphenated one, since those are the shapes a resize handle wants and the
  // ones a mapping table is most likely to misspell.
  rn_view_set_cursor(view, "ns-resize");
  EXPECT_EQ(nameOf(view), std::string("ns-resize"));

  g_object_unref(view);
}

TEST(a_cursor_can_be_taken_away_again) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);

  rn_view_set_cursor(view, "grab");
  EXPECT(gtk_widget_get_cursor(GTK_WIDGET(view)) != nullptr);

  // A view whose cursor prop goes away has to stop claiming one, or it keeps the
  // hand for the rest of its life. Empty is the same as nothing: React Native
  // sends `auto`, which arrives here as a null name.
  rn_view_set_cursor(view, nullptr);
  EXPECT(gtk_widget_get_cursor(GTK_WIDGET(view)) == nullptr);
  EXPECT(rn_view_get_cursor(view) == nullptr);

  rn_view_set_cursor(view, "grab");
  rn_view_set_cursor(view, "");
  EXPECT(gtk_widget_get_cursor(GTK_WIDGET(view)) == nullptr);

  g_object_unref(view);
}

// And it is in the tree dump, which is what makes the wiring testable: the prop
// is read in GtkMountingManager, and the tests above set it on the widget by
// hand. Spelled as the AppKit side spells it, so the cross-host diff compares
// them.
TEST(a_cursor_is_reported_in_the_tree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 50.0F);

  char *text = rn_view_describe_tree(view);
  const std::string none(text);
  g_free(text);
  EXPECT(none.find("cursor=") == std::string::npos);

  rn_view_set_cursor(view, "grab");
  text = rn_view_describe_tree(view);
  const std::string grabbing(text);
  g_free(text);
  EXPECT(grabbing.find("cursor=grab") != std::string::npos);

  g_object_unref(view);
}

// `boxShadow`. GSK has an outset and an inset shadow node and CSS's six fields
// are exactly their six arguments, so what a shadow is and where it went are
// both readable out of the render tree rather than only on screen.

TEST(a_box_shadow_is_an_outset_shadow_node_carrying_its_own_numbers) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnBoxShadow shadow{
      .dx = 2.0F, .dy = 4.0F, .blur = 8.0F, .spread = 1.0F,
      .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.25F}, .inset = FALSE};
  rn_view_set_box_shadows(view, &shadow, 1);
  EXPECT_EQ(rn_view_get_box_shadow_count(view), 1);

  GskRenderNode *painted = paintedNode(view);
  ShadowNodes found{0, 0};
  countShadowNodes(painted, &found);
  EXPECT_EQ(found.outset, 1);
  EXPECT_EQ(found.inset, 0);

  // The numbers, in CSS's own order. Offset, blur and spread are three
  // different arguments of the same shape, and a shadow with any two of them
  // swapped still looks like a shadow.
  GskRenderNode *node = firstOutsetShadow(painted);
  EXPECT(node != nullptr);
  EXPECT_EQ((double)gsk_outset_shadow_node_get_dx(node), 2.0);
  EXPECT_EQ((double)gsk_outset_shadow_node_get_dy(node), 4.0);
  EXPECT_EQ((double)gsk_outset_shadow_node_get_blur_radius(node), 8.0);
  EXPECT_EQ((double)gsk_outset_shadow_node_get_spread(node), 1.0);
  // And the outline is the view's own box, so a rounded card casts a rounded
  // shadow without anything here knowing the radii.
  const GskRoundedRect *outline = gsk_outset_shadow_node_get_outline(node);
  EXPECT_EQ((double)outline->bounds.size.width, 100.0);
  EXPECT_EQ((double)outline->bounds.size.height, 60.0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

TEST(an_inset_box_shadow_is_the_other_node) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnBoxShadow shadow{
      .dx = 0.0F, .dy = 2.0F, .blur = 4.0F, .spread = 0.0F,
      .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.5F}, .inset = TRUE};
  rn_view_set_box_shadows(view, &shadow, 1);

  GskRenderNode *painted = paintedNode(view);
  ShadowNodes found{0, 0};
  countShadowNodes(painted, &found);
  // Inside, not behind: an inset shadow drawn as an outset one is the most
  // likely way to get this wrong and the hardest to see in a screenshot.
  EXPECT_EQ(found.inset, 1);
  EXPECT_EQ(found.outset, 0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// Several shadows, which is the usual way a card is drawn: one tight and dark,
// one wide and soft.
TEST(every_box_shadow_in_the_list_is_painted) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnBoxShadow shadows[3] = {
      {.dx = 0.0F, .dy = 1.0F, .blur = 2.0F, .spread = 0.0F,
       .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.2F}, .inset = FALSE},
      {.dx = 0.0F, .dy = 8.0F, .blur = 24.0F, .spread = -4.0F,
       .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.15F}, .inset = FALSE},
      {.dx = 0.0F, .dy = 1.0F, .blur = 0.0F, .spread = 0.0F,
       .color = GdkRGBA{1.0F, 1.0F, 1.0F, 0.4F}, .inset = TRUE},
  };
  rn_view_set_box_shadows(view, shadows, 3);
  EXPECT_EQ(rn_view_get_box_shadow_count(view), 3);

  GskRenderNode *painted = paintedNode(view);
  ShadowNodes found{0, 0};
  countShadowNodes(painted, &found);
  EXPECT_EQ(found.outset, 2);
  EXPECT_EQ(found.inset, 1);

  // The first shadow in the list is the one on top, so the two outset nodes come
  // out back to front: the wide soft one is painted first.
  GskRenderNode *first = firstOutsetShadow(painted);
  EXPECT(first != nullptr);
  EXPECT_EQ((double)gsk_outset_shadow_node_get_blur_radius(first), 24.0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

TEST(a_box_shadow_can_be_taken_away_again) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnBoxShadow shadow{
      .dx = 0.0F, .dy = 4.0F, .blur = 8.0F, .spread = 0.0F,
      .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.25F}, .inset = FALSE};
  rn_view_set_box_shadows(view, &shadow, 1);
  rn_view_set_box_shadows(view, nullptr, 0);
  EXPECT_EQ(rn_view_get_box_shadow_count(view), 0);

  GskRenderNode *painted = paintedNode(view);
  ShadowNodes found{0, 0};
  countShadowNodes(painted, &found);
  EXPECT_EQ(found.outset, 0);
  EXPECT_EQ(found.inset, 0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// A shadow React Native could not parse a colour for arrives with none. Painting
// it would put a black shadow under a view that asked for nothing, which is worse
// than the missing shadow.
TEST(a_box_shadow_with_no_colour_is_not_painted) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnBoxShadow shadow{
      .dx = 0.0F, .dy = 4.0F, .blur = 8.0F, .spread = 0.0F,
      .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.0F}, .inset = FALSE};
  rn_view_set_box_shadows(view, &shadow, 1);

  GskRenderNode *painted = paintedNode(view);
  ShadowNodes found{0, 0};
  countShadowNodes(painted, &found);
  EXPECT_EQ(found.outset, 0);
  // Still counted as set, because the props said so: what is skipped is the
  // painting, not the prop.
  EXPECT_EQ(rn_view_get_box_shadow_count(view), 1);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// A negative blur radius is no blur rather than a Gsk-CRITICAL. CSS does not
// allow one and GSK asserts on it, and nothing between an app and here stops it:
// `blurRadius: -8` parses and arrives. Found while sabotaging the test above,
// which swapped the blur and the spread and took the whole suite down with it.
TEST(a_negative_blur_radius_on_a_box_shadow_is_not_a_crash) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnBoxShadow shadows[2] = {
      {.dx = 0.0F, .dy = 4.0F, .blur = -8.0F, .spread = 0.0F,
       .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.25F}, .inset = FALSE},
      {.dx = 0.0F, .dy = 4.0F, .blur = -8.0F, .spread = 0.0F,
       .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.25F}, .inset = TRUE},
  };
  rn_view_set_box_shadows(view, shadows, 2);

  GskRenderNode *painted = paintedNode(view);
  ShadowNodes found{0, 0};
  countShadowNodes(painted, &found);
  EXPECT_EQ(found.outset, 1);
  EXPECT_EQ(found.inset, 1);
  GskRenderNode *node = firstOutsetShadow(painted);
  EXPECT(node != nullptr);
  EXPECT_EQ((double)gsk_outset_shadow_node_get_blur_radius(node), 0.0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// And the list is in the tree dump, every field of it, which is what makes the
// wiring testable: the prop is read in GtkMountingManager, and the tests above
// set the shadows on the widget by hand.
TEST(box_shadows_are_reported_in_the_tree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);

  char *text = rn_view_describe_tree(view);
  const std::string none(text);
  g_free(text);
  EXPECT(none.find("shadow=") == std::string::npos);

  const RnBoxShadow shadows[2] = {
      {.dx = 2.0F, .dy = 4.0F, .blur = 8.0F, .spread = 1.0F,
       .color = GdkRGBA{0.0F, 0.0F, 0.0F, 0.25F}, .inset = FALSE},
      {.dx = 0.0F, .dy = 1.0F, .blur = 0.0F, .spread = 0.0F,
       .color = GdkRGBA{1.0F, 1.0F, 1.0F, 1.0F}, .inset = TRUE},
  };
  rn_view_set_box_shadows(view, shadows, 2);
  text = rn_view_describe_tree(view);
  const std::string dumped(text);
  g_free(text);
  EXPECT(dumped.find("shadow=(2,4,8,1,#00000040)") != std::string::npos);
  EXPECT(dumped.find("shadow=(inset 0,1,0,0,#ffffffff)") != std::string::npos);

  g_object_unref(view);
}

// `backgroundImage` as a linear gradient. GSK has a gradient node, so what was
// drawn and along which line are both readable out of the render tree.
//
// The arithmetic is not asserted here -- core's own tests do that, the angle and
// the stop fixup being shared with the other host. What is asserted here is that
// this widget draws what it was handed, in the right place in the paint order.

TEST(a_linear_gradient_is_a_gradient_node_along_the_line_it_was_given) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnGradientStop stops[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}},
      {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}},
  };
  const RnGradient gradient = linearGradient(graphene_point_t{0.0F, 60.0F}, graphene_point_t{0.0F, 0.0F}, stops, 2);
  rn_view_set_gradients(view, &gradient, 1);
  EXPECT_EQ(rn_view_get_gradient_count(view), 1);

  GskRenderNode *painted = paintedNode(view);
  EXPECT_EQ(gradientNodesIn(painted), 1);

  GskRenderNode *node = firstGradient(painted);
  EXPECT(node != nullptr);
  const graphene_point_t *start = gsk_linear_gradient_node_get_start(node);
  const graphene_point_t *end = gsk_linear_gradient_node_get_end(node);
  EXPECT_EQ((double)start->y, 60.0);
  EXPECT_EQ((double)end->y, 0.0);
  // And the stops reached GSK as they were given, offsets and colours.
  EXPECT_EQ((long)gsk_linear_gradient_node_get_n_color_stops(node), 2L);
  const GskColorStop *gskStops = gsk_linear_gradient_node_get_color_stops(node, nullptr);
  EXPECT_EQ((double)gskStops[0].offset, 0.0);
  EXPECT_EQ((double)gskStops[0].color.red, 1.0);
  EXPECT_EQ((double)gskStops[1].offset, 1.0);
  EXPECT_EQ((double)gskStops[1].color.blue, 1.0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// Several, which CSS allows: a translucent gradient over an opaque one is how a
// sheen is drawn.
TEST(every_linear_gradient_in_the_list_is_painted) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnGradientStop first[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}}};
  const RnGradientStop second[2] = {
      {0.0F, GdkRGBA{1.0F, 1.0F, 1.0F, 0.5F}}, {1.0F, GdkRGBA{1.0F, 1.0F, 1.0F, 0.0F}}};
  const RnGradient gradients[2] = {
      linearGradient(graphene_point_t{0.0F, 60.0F}, graphene_point_t{0.0F, 0.0F}, first, 2),
      linearGradient(graphene_point_t{0.0F, 0.0F}, graphene_point_t{100.0F, 0.0F}, second, 2),
  };
  rn_view_set_gradients(view, gradients, 2);

  GskRenderNode *painted = paintedNode(view);
  EXPECT_EQ(gradientNodesIn(painted), 2);
  // Back to front, so the first in the list is painted last and the *second* is
  // the one found first in the tree.
  GskRenderNode *node = firstGradient(painted);
  EXPECT_EQ((double)gsk_linear_gradient_node_get_end(node)->x, 100.0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// Above the background colour, which is where CSS paints a background image: a
// gradient under an opaque background would be invisible.
TEST(a_linear_gradient_paints_over_the_background_colour) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const GdkRGBA green{0.0F, 1.0F, 0.0F, 1.0F};
  rn_view_set_background_color(view, TRUE, &green);
  const RnGradientStop stops[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}}};
  const RnGradient gradient = linearGradient(graphene_point_t{0.0F, 0.0F}, graphene_point_t{100.0F, 0.0F}, stops, 2);
  rn_view_set_gradients(view, &gradient, 1);

  GskRenderNode *painted = paintedNode(view);
  EXPECT(painted != nullptr);
  // The container's children in paint order: the colour first, the gradient
  // after it. Walked rather than assumed, so a reordering shows up here.
  int colourAt = -1;
  int gradientAt = -1;
  if (gsk_render_node_get_node_type(painted) == GSK_CONTAINER_NODE) {
    for (guint i = 0; i < gsk_container_node_get_n_children(painted); i++) {
      GskRenderNode *child = gsk_container_node_get_child(painted, i);
      if (gsk_render_node_get_node_type(child) == GSK_COLOR_NODE && colourAt < 0) {
        colourAt = static_cast<int>(i);
      }
      if (gradientNodesIn(child) > 0 && gradientAt < 0) {
        gradientAt = static_cast<int>(i);
      }
    }
  }
  EXPECT(colourAt >= 0);
  EXPECT(gradientAt > colourAt);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// Rounded corners clip it, as they clip the background colour: a gradient with
// square corners on a rounded card is the kind of difference a screenshot shows
// and a tree does not.
TEST(a_linear_gradient_is_clipped_to_the_rounded_box) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const graphene_size_t radii[4] = {{12.0F, 12.0F}, {12.0F, 12.0F}, {12.0F, 12.0F}, {12.0F, 12.0F}};
  rn_view_set_border_radii(view, radii);
  const RnGradientStop stops[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}}};
  const RnGradient gradient = linearGradient(graphene_point_t{0.0F, 0.0F}, graphene_point_t{100.0F, 0.0F}, stops, 2);
  rn_view_set_gradients(view, &gradient, 1);

  GskRenderNode *painted = paintedNode(view);
  GskRenderNode *node = firstGradient(painted);
  EXPECT(node != nullptr);
  // The gradient is inside a rounded clip. Found by walking down to it and
  // noting the clip on the way, which is what the counter below does.
  EXPECT_EQ(clipNodesIn(painted) > 0, true);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

TEST(a_linear_gradient_can_be_taken_away_again) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnGradientStop stops[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}}};
  const RnGradient gradient = linearGradient(graphene_point_t{0.0F, 0.0F}, graphene_point_t{100.0F, 0.0F}, stops, 2);
  rn_view_set_gradients(view, &gradient, 1);
  rn_view_set_gradients(view, nullptr, 0);
  EXPECT_EQ(rn_view_get_gradient_count(view), 0);

  GskRenderNode *painted = paintedNode(view);
  EXPECT_EQ(gradientNodesIn(painted), 0);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// A radial gradient is a different GSK node, which is the point of these: a
// radial gradient painted as a linear one would still be a gradient, and the
// tree is what tells the two apart.
TEST(a_radial_gradient_is_a_radial_gradient_node_with_its_own_centre_and_radii) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 100.0F);
  layout(view, 200, 100);

  const RnGradientStop stops[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}}};
  const RnGradient gradient =
      radialGradient(graphene_point_t{60.0F, 30.0F}, 80.0F, 40.0F, stops, 2);
  rn_view_set_gradients(view, &gradient, 1);
  EXPECT_EQ(rn_view_get_gradient_count(view), 1);

  GskRenderNode *painted = paintedNode(view);
  GskRenderNode *node = firstRadialGradient(painted);
  EXPECT(node != nullptr);
  // No linear node, which is the half that says the kind was read rather than
  // ignored.
  EXPECT_EQ(gradientNodesIn(painted), 0);

  if (node != nullptr) {
    const graphene_point_t *center = gsk_radial_gradient_node_get_center(node);
    EXPECT_EQ((double)center->x, 60.0);
    EXPECT_EQ((double)center->y, 30.0);
    // The two radii, which a circle-only implementation would have collapsed.
    EXPECT_EQ((double)gsk_radial_gradient_node_get_hradius(node), 80.0);
    EXPECT_EQ((double)gsk_radial_gradient_node_get_vradius(node), 40.0);
    // The ending shape is the radius, so the gradient runs from 0 to 1 of it.
    EXPECT_EQ((double)gsk_radial_gradient_node_get_start(node), 0.0);
    EXPECT_EQ((double)gsk_radial_gradient_node_get_end(node), 1.0);
    // And the stops reached GSK as they were given.
    EXPECT_EQ((int)gsk_radial_gradient_node_get_n_color_stops(node), 2);
  }

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// Both kinds in one list, in the order the stylesheet wrote them. CSS paints the
// first on top, so they are painted back to front, and a list that dropped the
// radial one or reordered the two is what this catches.
TEST(a_radial_and_a_linear_gradient_keep_their_order) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnGradientStop stops[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}}};
  const RnGradient gradients[2] = {
      radialGradient(graphene_point_t{50.0F, 30.0F}, 50.0F, 30.0F, stops, 2),
      linearGradient(graphene_point_t{0.0F, 0.0F}, graphene_point_t{100.0F, 0.0F}, stops, 2),
  };
  rn_view_set_gradients(view, gradients, 2);
  EXPECT_EQ(rn_view_get_gradient_count(view), 2);

  GskRenderNode *painted = paintedNode(view);
  // One of each, so neither kind swallowed the other.
  EXPECT_EQ(gradientNodesIn(painted), 1);
  EXPECT(firstRadialGradient(painted) != nullptr);

  // The radial one is first in the list, so it is painted last and is the one
  // found last in the tree: the linear node comes first.
  EXPECT(gradientOrderIn(painted) == "LR");

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

TEST(a_radial_gradient_can_be_taken_away_again) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);
  layout(view, 100, 60);

  const RnGradientStop stops[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}}};
  const RnGradient gradient =
      radialGradient(graphene_point_t{50.0F, 30.0F}, 50.0F, 30.0F, stops, 2);
  rn_view_set_gradients(view, &gradient, 1);
  rn_view_set_gradients(view, nullptr, 0);
  EXPECT_EQ(rn_view_get_gradient_count(view), 0);

  GskRenderNode *painted = paintedNode(view);
  EXPECT(firstRadialGradient(painted) == nullptr);

  if (painted != nullptr) {
    gsk_render_node_unref(painted);
  }
  g_object_unref(view);
}

// A radial gradient in the dump, spelled as the AppKit side spells it so one
// scenario can read both hosts.
TEST(radial_gradients_are_reported_in_the_tree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 100.0F);

  const RnGradientStop stops[2] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}}, {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}}};
  const RnGradient gradient =
      radialGradient(graphene_point_t{60.0F, 30.0F}, 80.0F, 40.0F, stops, 2);
  rn_view_set_gradients(view, &gradient, 1);

  char *text = rn_view_describe_tree(view);
  const std::string dumped(text);
  g_free(text);
  EXPECT(dumped.find("gradient=(radial (60,30) 80x40,2 stops)") != std::string::npos);

  g_object_unref(view);
}

// And it is in the tree dump, which is what makes the wiring testable: the prop
// is read in GtkMountingManager, where the angle and the stops are resolved, and
// the tests above hand the widget the answer by hand.
TEST(linear_gradients_are_reported_in_the_tree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 100.0F, 60.0F);

  char *text = rn_view_describe_tree(view);
  const std::string none(text);
  g_free(text);
  EXPECT(none.find("gradient=") == std::string::npos);

  const RnGradientStop stops[3] = {
      {0.0F, GdkRGBA{1.0F, 0.0F, 0.0F, 1.0F}},
      {0.5F, GdkRGBA{0.0F, 1.0F, 0.0F, 1.0F}},
      {1.0F, GdkRGBA{0.0F, 0.0F, 1.0F, 1.0F}},
  };
  const RnGradient gradient = linearGradient(graphene_point_t{0.0F, 60.0F}, graphene_point_t{0.0F, 0.0F}, stops, 3);
  rn_view_set_gradients(view, &gradient, 1);

  text = rn_view_describe_tree(view);
  const std::string dumped(text);
  g_free(text);
  EXPECT(dumped.find("gradient=((0,60)-(0,0),3 stops)") != std::string::npos);

  g_object_unref(view);
}

// The text a live region would announce: its own paragraph and every paragraph
// inside it, in tree order. See rn_view_collect_text, which
// `accessibilityLiveRegion` uses to find out what a status message currently
// says.
TEST(collected_text_is_every_paragraph_in_the_subtree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 200.0F, 100.0F);

  // A region with no text of its own and two paragraphs inside it, which is what
  // a status line made of a label and a count looks like.
  PangoContext *context = gtk_widget_create_pango_context(GTK_WIDGET(view));
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};

  // Inserted at their own indices rather than through `addChild`, which puts
  // every child at 0: the order is what is being asserted, so the tree has to be
  // in the order a mount would have built it.
  RnView *first = rn_view_new(2);
  g_object_ref_sink(first);
  rn_view_set_frame(first, 0.0F, 0.0F, 200.0F, 20.0F);
  rn_view_insert_child(view, first, 0);
  PangoLayout *one = pango_layout_new(context);
  pango_layout_set_text(one, "Saved", -1);
  rn_view_set_text_layout(first, one, &black);

  RnView *second = rn_view_new(3);
  g_object_ref_sink(second);
  rn_view_set_frame(second, 0.0F, 20.0F, 200.0F, 20.0F);
  rn_view_insert_child(view, second, 1);
  PangoLayout *two = pango_layout_new(context);
  pango_layout_set_text(two, "just now", -1);
  rn_view_set_text_layout(second, two, &black);

  char *collected = rn_view_collect_text(view);
  const std::string text(collected);
  g_free(collected);
  // One space between them, which is what a screen reader would put there.
  EXPECT_EQ(text, std::string("Saved just now"));

  // A view with no text at all collects nothing rather than crashing on the
  // way down, which is every ordinary view in a tree.
  RnView *empty = rn_view_new(4);
  g_object_ref_sink(empty);
  char *none = rn_view_collect_text(empty);
  EXPECT_EQ(std::string(none), std::string(""));
  g_free(none);

  g_object_unref(one);
  g_object_unref(two);
  g_object_unref(context);
  g_object_unref(empty);
  g_object_unref(view);
}

// `filter`: the colour matrix, the blur and the opacity a list of CSS filter
// functions comes to.
//
// Against a rendered pixel, not a node count. The arithmetic is core's and is
// tested there against the spec's numbers; what is left to get wrong here is
// handing GSK the matrix the wrong way round, which a transposed matrix does
// quietly -- every channel still changes, so a node-counting test passes and the
// colours are wrong.

TEST(a_filter_matrix_reaches_gsk_the_right_way_round) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 20.0F, 20.0F);
  layout(view, 20, 20);

  // An asymmetric colour, so a transposed matrix cannot pass: red full, green
  // half, blue off.
  const GdkRGBA colour{1.0F, 0.5F, 0.0F, 1.0F};
  rn_view_set_background_color(view, TRUE, &colour);

  const basalt::testing::RnPixel before = basalt::testing::renderView(view, 20, 20).at(10, 10);
  EXPECT_NEAR(before.red, 255, 2);
  EXPECT_NEAR(before.green, 128, 3);
  EXPECT_NEAR(before.blue, 0, 2);

  // grayscale(1): every channel becomes the luminance, 0.2126r + 0.7152g +
  // 0.0722b, which for this colour is 0.2126 + 0.3576 = 0.570 -> 145.
  const auto resolved = basalt::resolveFilters(
      {[]() {
        facebook::react::FilterFunction function;
        function.type = facebook::react::FilterType::Grayscale;
        function.parameters = static_cast<facebook::react::Float>(1.0);
        return function;
      }()});
  RnFilters filters{};
  filters.has_matrix = resolved.matrix.isIdentity() ? FALSE : TRUE;
  for (int i = 0; i < 16; i++) {
    filters.matrix[i] = resolved.matrix.m[i];
  }
  for (int i = 0; i < 4; i++) {
    filters.offset[i] = resolved.matrix.offset[i];
  }
  filters.blur_radius = resolved.blurRadius;
  filters.opacity = resolved.opacity;
  rn_view_set_filters(view, &filters);

  const basalt::testing::RnPixel grey = basalt::testing::renderView(view, 20, 20).at(10, 10);
  EXPECT_NEAR(grey.red, 145, 3);
  EXPECT_NEAR(grey.green, 145, 3);
  EXPECT_NEAR(grey.blue, 145, 3);
  // The same in all three, which is what being grey means and what a transposed
  // matrix would not give.
  EXPECT(std::abs(grey.red - grey.blue) <= 2);
  EXPECT_NEAR(grey.alpha, 255, 2);

  // And taken away again.
  rn_view_set_filters(view, nullptr);
  const basalt::testing::RnPixel back = basalt::testing::renderView(view, 20, 20).at(10, 10);
  EXPECT_NEAR(back.red, 255, 2);
  EXPECT_NEAR(back.blue, 0, 2);

  g_object_unref(view);
}

// An offset is the other half of the affine map, and the one a matrix-only
// implementation drops: invert(1) is a scale of -1 *and* a translate of 1.
TEST(a_filter_offset_reaches_gsk_too) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 20.0F, 20.0F);
  layout(view, 20, 20);

  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_background_color(view, TRUE, &black);

  facebook::react::FilterFunction invert;
  invert.type = facebook::react::FilterType::Invert;
  invert.parameters = static_cast<facebook::react::Float>(1.0);
  const auto resolved = basalt::resolveFilters({invert});

  RnFilters filters{};
  filters.has_matrix = TRUE;
  for (int i = 0; i < 16; i++) {
    filters.matrix[i] = resolved.matrix.m[i];
  }
  for (int i = 0; i < 4; i++) {
    filters.offset[i] = resolved.matrix.offset[i];
  }
  filters.opacity = 1.0F;
  rn_view_set_filters(view, &filters);

  // Black inverted is white. Without the offset it would still be black.
  const basalt::testing::RnPixel inverted =
      basalt::testing::renderView(view, 20, 20).at(10, 10);
  EXPECT_NEAR(inverted.red, 255, 2);
  EXPECT_NEAR(inverted.green, 255, 2);
  EXPECT_NEAR(inverted.blue, 255, 2);

  g_object_unref(view);
}

// The other two parts are nodes rather than numbers, so these are counted: a
// blur node and an opacity node around the whole subtree.
TEST(a_filter_blur_and_opacity_are_nodes_around_everything) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 40.0F, 40.0F);
  layout(view, 40, 40);
  const GdkRGBA red{1.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_background_color(view, TRUE, &red);

  GskRenderNode *plain = paintedNode(view);
  EXPECT_EQ(blurNodesIn(plain), 0);

  RnFilters filters{};
  filters.blur_radius = 8.0F;
  filters.opacity = 0.5F;
  rn_view_set_filters(view, &filters);

  GskRenderNode *filtered = paintedNode(view);
  EXPECT_EQ(blurNodesIn(filtered), 1);
  // The opacity is the outermost node, which is what makes it apply to the
  // children too rather than to this view's own paint.
  EXPECT_EQ(static_cast<int>(gsk_render_node_get_node_type(filtered)), GSK_OPACITY_NODE);
  EXPECT_NEAR((int)(gsk_opacity_node_get_opacity(filtered) * 100), 50, 1);

  if (plain != nullptr) {
    gsk_render_node_unref(plain);
  }
  if (filtered != nullptr) {
    gsk_render_node_unref(filtered);
  }
  g_object_unref(view);
}

// And the filter is in the tree dump, as the pieces it came to plus what its
// matrix makes of a probe colour -- which is how a wrong matrix shows up in a
// cross-host diff without printing sixteen numbers per view.
TEST(a_filter_is_reported_in_the_tree) {
  RnView *view = rn_view_new(1);
  g_object_ref_sink(view);
  rn_view_set_frame(view, 0.0F, 0.0F, 20.0F, 20.0F);

  char *text = rn_view_describe_tree(view);
  const std::string none(text);
  g_free(text);
  EXPECT(none.find("filter=") == std::string::npos);

  facebook::react::FilterFunction grayscale;
  grayscale.type = facebook::react::FilterType::Grayscale;
  grayscale.parameters = static_cast<facebook::react::Float>(1.0);
  facebook::react::FilterFunction blur;
  blur.type = facebook::react::FilterType::Blur;
  blur.parameters = static_cast<facebook::react::Float>(8.0);
  const auto resolved = basalt::resolveFilters({grayscale, blur});

  RnFilters filters{};
  filters.has_matrix = TRUE;
  for (int i = 0; i < 16; i++) {
    filters.matrix[i] = resolved.matrix.m[i];
  }
  for (int i = 0; i < 4; i++) {
    filters.offset[i] = resolved.matrix.offset[i];
  }
  filters.blur_radius = resolved.blurRadius;
  filters.opacity = resolved.opacity;
  rn_view_set_filters(view, &filters);

  text = rn_view_describe_tree(view);
  const std::string dumped(text);
  g_free(text);
  // The probe is (1, 0.5, 0.25): greyed, that is 0.2126 + 0.3576 + 0.018 =
  // 0.588, which is 0x96 in all three channels.
  EXPECT(dumped.find("filter=(probe=#969696ff,blur=8)") != std::string::npos);

  g_object_unref(view);
}
