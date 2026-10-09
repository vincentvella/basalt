// What actually reached the pixels, on GTK.
//
// Deliberately the same questions `tests/test_win32_paint.cpp` asks of Direct2D,
// in the same order, because the answer is supposed to be the same picture: a
// child at its frame, opacity over a whole subtree, a clip that clips, children
// in zIndex order, a scroll offset, the background inside its corner radii, and
// a border inside the box and over the children.
//
// `docs/backlog/testing.md` carried "no rendering assertions on GTK" from the
// start, on the understanding that pixels here need a display server and a shown
// window. They do not -- see tests/GtkPixels.h -- and the bug the entry was
// written about is exactly what a tree dump cannot catch: a clip that does not
// clip, an opacity applied per-node instead of per-subtree, and a zIndex that
// reorders nothing all leave the dump identical and the picture wrong.

#include "TestHarness.h"

#include "GtkPixels.h"
#include "RnView.h"

#include <sstream>

using basalt::testing::RnPixel;
using basalt::testing::RnPixels;
using basalt::testing::renderView;

namespace {

// Rounding through premultiplied storage costs a unit or two, and both renderers
// antialias an edge -- so every point asked about below is well inside or well
// outside a shape, never on its boundary.
constexpr double kTolerance = 3.0;

// A tree that cleans itself up: every view is sunk and unreffed at the end, which
// matters here because a GtkWidget holds its children.
class Tree {
 public:
  ~Tree() {
    for (RnView *view : roots_) {
      g_object_unref(view);
    }
  }

  Tree() = default;
  Tree(const Tree &) = delete;
  Tree &operator=(const Tree &) = delete;

  RnView *root(int tag, float width, float height) {
    RnView *view = rn_view_new(tag);
    g_object_ref_sink(view);
    rn_view_set_frame(view, 0.0F, 0.0F, width, height);
    roots_.push_back(view);
    return view;
  }

  RnView *box(RnView *parent, int tag, float x, float y, float width, float height) {
    RnView *view = rn_view_new(tag);
    g_object_ref_sink(view);
    rn_view_set_frame(view, x, y, width, height);
    rn_view_insert_child(parent, view, childCount_[parent]++);
    return view;
  }

  RnView *colouredBox(RnView *parent,
                      int tag,
                      float x,
                      float y,
                      float width,
                      float height,
                      float red,
                      float green,
                      float blue) {
    RnView *view = box(parent, tag, x, y, width, height);
    const GdkRGBA colour{red, green, blue, 1.0F};
    rn_view_set_background_color(view, TRUE, &colour);
    return view;
  }

 private:
  std::vector<RnView *> roots_;
  std::unordered_map<RnView *, int> childCount_;
};

} // namespace

// A macro rather than a function so a failure reports the line that asked, which
// is the only thing that makes a wrong pixel findable.
#define EXPECT_PIXEL(pixels, x, y, r, g, b, a)                                                     \
  do {                                                                                             \
    const RnPixel pixel = (pixels).at((x), (y));                                                   \
    EXPECT_NEAR(pixel.red, (r), kTolerance);                                                       \
    EXPECT_NEAR(pixel.green, (g), kTolerance);                                                     \
    EXPECT_NEAR(pixel.blue, (b), kTolerance);                                                      \
    EXPECT_NEAR(pixel.alpha, (a), kTolerance);                                                      \
  } while (false)

#define EXPECT_TRANSPARENT(pixels, x, y) EXPECT_NEAR((pixels).at((x), (y)).alpha, 0, kTolerance)

// The first one also checks the harness itself: a renderer realized, and the
// image is not uniformly empty.
TEST(gtk_paint_puts_a_child_at_its_frame) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  tree.colouredBox(root, 2, 20, 30, 40, 20, 1.0F, 0.0F, 0.0F);

  const RnPixels pixels = renderView(root, 100, 100);
  EXPECT(pixels.valid());
  EXPECT(!pixels.rendererName().empty());

  // Inside the child.
  EXPECT_PIXEL(pixels, 40, 40, 255, 0, 0, 255);
  // Outside it on every side, where the root paints nothing at all.
  EXPECT_TRANSPARENT(pixels, 40, 20);
  EXPECT_TRANSPARENT(pixels, 40, 60);
  EXPECT_TRANSPARENT(pixels, 10, 40);
  EXPECT_TRANSPARENT(pixels, 70, 40);
}

// Opacity is one layer over the subtree, not a per-view alpha. Two overlapping
// opaque children under a half-opacity parent come out at half alpha, and the
// overlap is *not* three quarters -- which is what applying it per view gives.
TEST(gtk_paint_composites_opacity_over_the_whole_subtree) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *faded = tree.box(root, 2, 0, 0, 100, 100);
  rn_view_set_opacity(faded, 0.5);
  tree.colouredBox(faded, 3, 10, 10, 40, 40, 1.0F, 0.0F, 0.0F);
  tree.colouredBox(faded, 4, 30, 30, 40, 40, 1.0F, 0.0F, 0.0F);

  const RnPixels pixels = renderView(root, 100, 100);
  // One child alone.
  EXPECT_NEAR(pixels.at(20, 20).alpha, 128, 4);
  // And where they overlap: still half, because the layer is composited once.
  EXPECT_NEAR(pixels.at(40, 40).alpha, 128, 4);
}

// `overflow: hidden` clips children; the default does not. React Native's
// default is `visible`, and a host that clipped anyway would cut off every
// child that overflows its parent -- which is most of a shadowed card.
TEST(gtk_paint_clips_children_only_when_overflow_is_hidden) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *parent = tree.box(root, 2, 10, 10, 30, 30);
  tree.colouredBox(parent, 3, 0, 0, 60, 60, 0.0F, 0.0F, 1.0F);

  const RnPixels visible = renderView(root, 100, 100);
  // Beyond the parent's 30x30 box and still painted.
  EXPECT_PIXEL(visible, 55, 55, 0, 0, 255, 255);

  rn_view_set_clips_children(parent, TRUE);
  const RnPixels clipped = renderView(root, 100, 100);
  EXPECT_PIXEL(clipped, 20, 20, 0, 0, 255, 255);
  EXPECT_TRANSPARENT(clipped, 55, 55);
}

// zIndex decides what is painted last, which is not the order the children are
// in. A host that ignored it leaves the tree dump identical and the picture
// upside down.
TEST(gtk_paint_orders_children_by_z_index) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *first = tree.colouredBox(root, 2, 10, 10, 50, 50, 1.0F, 0.0F, 0.0F);
  tree.colouredBox(root, 3, 30, 30, 50, 50, 0.0F, 0.0F, 1.0F);

  // Later child wins the overlap by default.
  const RnPixels byOrder = renderView(root, 100, 100);
  EXPECT_PIXEL(byOrder, 45, 45, 0, 0, 255, 255);

  // And the first one wins it once it asks to be on top.
  rn_view_set_z_index(first, 1);
  const RnPixels byIndex = renderView(root, 100, 100);
  EXPECT_PIXEL(byIndex, 45, 45, 255, 0, 0, 255);
}

// A scroll offset really moves the children, which is what makes GTK's own
// picking follow a scrolled view without any special case.
TEST(gtk_paint_moves_children_by_the_scroll_offset) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *scroller = tree.box(root, 2, 0, 0, 100, 100);
  rn_view_set_clips_children(scroller, TRUE);
  tree.colouredBox(scroller, 3, 0, 60, 100, 40, 0.0F, 0.6F, 0.0F);

  const RnPixels still = renderView(root, 100, 100);
  EXPECT_TRANSPARENT(still, 50, 30);
  EXPECT_NEAR(still.at(50, 80).green, 153, kTolerance);

  // Scrolled down by 60, the row that was at the bottom is at the top.
  rn_view_set_scroll_offset(scroller, 0.0, 60.0);
  const RnPixels scrolled = renderView(root, 100, 100);
  EXPECT_NEAR(scrolled.at(50, 20).green, 153, kTolerance);
}

// The background is clipped to the corner radius even when children are not,
// which is the arrangement `overflow: visible` on a rounded card needs.
TEST(gtk_paint_rounds_the_background_to_the_corner_radius) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *rounded = tree.colouredBox(root, 2, 0, 0, 100, 100, 1.0F, 0.0F, 0.0F);
  const graphene_size_t radii[4] = {
      {30.0F, 30.0F}, {30.0F, 30.0F}, {30.0F, 30.0F}, {30.0F, 30.0F}};
  rn_view_set_border_radii(rounded, radii);

  const RnPixels pixels = renderView(root, 100, 100);
  // The middle is filled and the corner is not: (3,3) is outside a 30pt radius.
  EXPECT_PIXEL(pixels, 50, 50, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(pixels, 3, 3);
  EXPECT_TRANSPARENT(pixels, 96, 3);
  EXPECT_TRANSPARENT(pixels, 3, 96);
  EXPECT_TRANSPARENT(pixels, 96, 96);
}

// Each corner on its own, which four radii in one array is four chances to get
// wrong: a top-left radius applied to the bottom-right passes any symmetric
// test.
TEST(gtk_paint_rounds_each_corner_on_its_own) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *rounded = tree.colouredBox(root, 2, 0, 0, 100, 100, 1.0F, 0.0F, 0.0F);
  // Top-left only, in the order GTK's rounded rect wants: top-left, top-right,
  // bottom-right, bottom-left.
  const graphene_size_t radii[4] = {{40.0F, 40.0F}, {0.0F, 0.0F}, {0.0F, 0.0F}, {0.0F, 0.0F}};
  rn_view_set_border_radii(rounded, radii);

  const RnPixels pixels = renderView(root, 100, 100);
  EXPECT_TRANSPARENT(pixels, 3, 3);
  // And the other three corners are square.
  EXPECT_PIXEL(pixels, 96, 3, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 3, 96, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 96, 96, 255, 0, 0, 255);
}

// A border is drawn inside the box, as CSS says: it eats into the view rather
// than growing it.
TEST(gtk_paint_draws_a_border_inside_the_box) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *bordered = tree.colouredBox(root, 2, 10, 10, 80, 80, 1.0F, 1.0F, 1.0F);
  const float widths[4] = {10.0F, 10.0F, 10.0F, 10.0F};
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  const GdkRGBA colours[4] = {black, black, black, black};
  rn_view_set_borders(bordered, widths, colours);

  const RnPixels pixels = renderView(root, 100, 100);
  // Just inside the view's own edge: the border.
  EXPECT_PIXEL(pixels, 50, 15, 0, 0, 0, 255);
  // Past it: the background.
  EXPECT_PIXEL(pixels, 50, 50, 255, 255, 255, 255);
  // Just outside the view: nothing, which is what "inside the box" means.
  EXPECT_TRANSPARENT(pixels, 50, 5);
}

// Each edge its own colour, which is what a mitre is for: the corner belongs
// half to one edge and half to the other.
TEST(gtk_paint_colours_each_edge_on_its_own) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *bordered = tree.colouredBox(root, 2, 0, 0, 100, 100, 1.0F, 1.0F, 1.0F);
  const float widths[4] = {20.0F, 20.0F, 20.0F, 20.0F};
  // Top red, right green, bottom blue, left black -- the order CSS names them.
  const GdkRGBA colours[4] = {{1.0F, 0.0F, 0.0F, 1.0F},
                              {0.0F, 1.0F, 0.0F, 1.0F},
                              {0.0F, 0.0F, 1.0F, 1.0F},
                              {0.0F, 0.0F, 0.0F, 1.0F}};
  rn_view_set_borders(bordered, widths, colours);

  const RnPixels pixels = renderView(root, 100, 100);
  EXPECT_PIXEL(pixels, 50, 5, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 95, 50, 0, 255, 0, 255);
  EXPECT_PIXEL(pixels, 50, 95, 0, 0, 255, 255);
  EXPECT_PIXEL(pixels, 5, 50, 0, 0, 0, 255);
}

// The border paints over the children, as it does on every other platform: a
// child that fills its parent must not hide the parent's border.
TEST(gtk_paint_draws_the_border_over_the_children) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *bordered = tree.box(root, 2, 0, 0, 100, 100);
  tree.colouredBox(bordered, 3, 0, 0, 100, 100, 1.0F, 1.0F, 1.0F);
  const float widths[4] = {10.0F, 10.0F, 10.0F, 10.0F};
  const GdkRGBA black{0.0F, 0.0F, 0.0F, 1.0F};
  const GdkRGBA colours[4] = {black, black, black, black};
  rn_view_set_borders(bordered, widths, colours);

  const RnPixels pixels = renderView(root, 100, 100);
  EXPECT_PIXEL(pixels, 50, 5, 0, 0, 0, 255);
  EXPECT_PIXEL(pixels, 50, 50, 255, 255, 255, 255);
}

// And a view with nothing to draw draws nothing, which is the control for every
// assertion above: an image that came back opaque black would pass several of
// them by accident.
TEST(gtk_paint_a_plain_view_paints_nothing) {
  Tree tree;
  RnView *root = tree.root(1, 60, 60);
  tree.box(root, 2, 10, 10, 20, 20);

  const RnPixels pixels = renderView(root, 60, 60);
  EXPECT_TRANSPARENT(pixels, 20, 20);
  EXPECT_TRANSPARENT(pixels, 50, 50);
}

// CSS's outline: `outlineWidth`, `outlineColor`, `outlineOffset` and
// `outlineStyle`.
//
// Not a border, which is the whole of what makes it worth asserting in pixels: it
// is drawn *outside* the box and takes no layout space, so what proves it is ink
// beyond the view's own frame with the frame unchanged. Web-ported code sets it
// for a focus ring and expects exactly that.
TEST(gtk_paint_draws_an_outline_outside_the_box) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *view = tree.colouredBox(root, 2, 30, 30, 40, 40, 1.0F, 1.0F, 1.0F);
  const GdkRGBA red{1.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_outline(view, 4.0F, 0.0F, &red, RN_BORDER_SOLID);

  const RnPixels pixels = renderView(root, 100, 100);
  // Two points outside the box's left edge: inside a four point outline.
  EXPECT_PIXEL(pixels, 28, 50, 255, 0, 0, 255);
  // And on every other side, so a ring rather than one edge.
  EXPECT_PIXEL(pixels, 72, 50, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 50, 28, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 50, 72, 255, 0, 0, 255);

  // The box itself is untouched: an outline that ate into the view would be a
  // border, and the two are different props.
  EXPECT_PIXEL(pixels, 50, 50, 255, 255, 255, 255);
  EXPECT_PIXEL(pixels, 32, 50, 255, 255, 255, 255);
  // Past the outline, nothing.
  EXPECT_TRANSPARENT(pixels, 20, 50);
}

// `outlineOffset` is the gap between the box and the ring, which is what makes a
// focus ring look deliberate rather than like a second border.
TEST(gtk_paint_an_outline_offset_leaves_a_gap) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *view = tree.colouredBox(root, 2, 30, 30, 40, 40, 1.0F, 1.0F, 1.0F);
  const GdkRGBA red{1.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_outline(view, 4.0F, 6.0F, &red, RN_BORDER_SOLID);

  const RnPixels pixels = renderView(root, 100, 100);
  // The gap: three points out from the edge, between the box and the ring.
  EXPECT_TRANSPARENT(pixels, 27, 50);
  // The ring itself, eight points out.
  EXPECT_PIXEL(pixels, 22, 50, 255, 0, 0, 255);
  // And past it again.
  EXPECT_TRANSPARENT(pixels, 16, 50);
}

// A dashed outline leaves gaps along the ring, the way a dashed border does.
TEST(gtk_paint_a_dashed_outline_is_not_solid) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *view = tree.colouredBox(root, 2, 20, 20, 60, 60, 1.0F, 1.0F, 1.0F);
  const GdkRGBA red{1.0F, 0.0F, 0.0F, 1.0F};

  rn_view_set_outline(view, 4.0F, 0.0F, &red, RN_BORDER_SOLID);
  const RnPixels solid = renderView(root, 100, 100);
  int solidInk = 0;
  for (int x = 0; x < 100; x++) {
    if (solid.at(x, 18).alpha > 40) {
      solidInk++;
    }
  }

  rn_view_set_outline(view, 4.0F, 0.0F, &red, RN_BORDER_DASHED);
  const RnPixels dashed = renderView(root, 100, 100);
  int dashedInk = 0;
  for (int x = 0; x < 100; x++) {
    if (dashed.at(x, 18).alpha > 40) {
      dashedInk++;
    }
  }

  // Ink either way, and less of it when it is dashed.
  EXPECT(solidInk > 50);
  EXPECT(dashedInk > 0);
  EXPECT(dashedInk < solidInk);
}

TEST(gtk_paint_no_outline_width_is_no_outline) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *view = tree.colouredBox(root, 2, 30, 30, 40, 40, 1.0F, 1.0F, 1.0F);
  const GdkRGBA red{1.0F, 0.0F, 0.0F, 1.0F};

  rn_view_set_outline(view, 4.0F, 0.0F, &red, RN_BORDER_SOLID);
  EXPECT_PIXEL(renderView(root, 100, 100), 28, 50, 255, 0, 0, 255);

  // Taken away again, which is what losing focus does.
  rn_view_set_outline(view, 0.0F, 0.0F, &red, RN_BORDER_SOLID);
  EXPECT_TRANSPARENT(renderView(root, 100, 100), 28, 50);

  // And a width with no colour paints nothing: React Native's default outline
  // colour is undefined, which arrives as a transparent one.
  const GdkRGBA invisible{0.0F, 0.0F, 0.0F, 0.0F};
  rn_view_set_outline(view, 4.0F, 0.0F, &invisible, RN_BORDER_SOLID);
  EXPECT_TRANSPARENT(renderView(root, 100, 100), 28, 50);
}

// The ring follows the view's corners, grown by the width and the offset so it
// stays concentric: a square ring around a rounded card is the giveaway that the
// radii were not carried over.
TEST(gtk_paint_an_outline_follows_the_corner_radius) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *view = tree.colouredBox(root, 2, 20, 20, 60, 60, 1.0F, 1.0F, 1.0F);
  const graphene_size_t radii[4] = {
      {20.0F, 20.0F}, {20.0F, 20.0F}, {20.0F, 20.0F}, {20.0F, 20.0F}};
  rn_view_set_border_radii(view, radii);
  const GdkRGBA red{1.0F, 0.0F, 0.0F, 1.0F};
  rn_view_set_outline(view, 4.0F, 0.0F, &red, RN_BORDER_SOLID);

  const RnPixels pixels = renderView(root, 100, 100);
  // The middle of an edge is on the ring.
  EXPECT_PIXEL(pixels, 18, 50, 255, 0, 0, 255);
  // The corner of the bounding box is outside a 24 point radius, so it is
  // empty -- where a square ring would paint it.
  EXPECT_TRANSPARENT(pixels, 17, 17);
}

// `mixBlendMode`, which is the one view prop whose whole observable is what it
// does to the pixels beneath it. A dump can say the keyword arrived; only a
// rendered picture can say the blend happened, and happened against the right
// backdrop.
//
// Every case below uses a mid grey child over a red parent, because the three
// answers that matter are far apart: multiply is (128,0,0), screen is
// (255,128,128) and no blend at all is (128,128,128).
namespace {

// A red parent with a grey child in the middle of it, which is the arrangement
// every blend test here starts from. The child is returned; the parent is at
// (10,10) so a pixel at (50,50) is inside both.
RnView *greyOnRed(Tree &tree, RnView *root, const char *blend) {
  RnView *parent = tree.colouredBox(root, 2, 10, 10, 80, 80, 1.0F, 0.0F, 0.0F);
  RnView *child = tree.colouredBox(parent, 3, 20, 20, 40, 40, 0.5F, 0.5F, 0.5F);
  rn_view_set_blend_mode(child, blend);
  return child;
}

} // namespace

TEST(gtk_paint_multiply_blends_a_child_with_its_parent) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  greyOnRed(tree, root, "multiply");

  // 0.5 * red is half red, and the green and blue the child had are multiplied
  // away by the parent's zeroes. Unblended this pixel is (128,128,128).
  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 128, 0, 0, 255);
}

// A second mode, because one mode can be hard-coded and still pass the first
// test. Screen is multiply's opposite and lands somewhere neither of the other
// two answers is.
TEST(gtk_paint_screen_is_not_multiply) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  greyOnRed(tree, root, "screen");

  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 255, 128, 128, 255);
}

// The backdrop is everything beneath the child, not only its parent's
// background. A blend that only saw the parent would come out half red here;
// one that sees the sibling comes out half blue.
TEST(gtk_paint_a_blend_sees_the_siblings_beneath_it) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *parent = tree.colouredBox(root, 2, 10, 10, 80, 80, 1.0F, 0.0F, 0.0F);
  tree.colouredBox(parent, 3, 20, 20, 40, 40, 0.0F, 0.0F, 1.0F);
  RnView *blended = tree.colouredBox(parent, 4, 20, 20, 40, 40, 0.5F, 0.5F, 0.5F);
  rn_view_set_blend_mode(blended, "multiply");

  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 0, 0, 128, 255);
}

// The negative control, and the way back: `mixBlendMode: 'normal'` arrives as no
// keyword at all, and a view that was blending has to stop.
TEST(gtk_paint_no_blend_mode_is_no_blending) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *child = greyOnRed(tree, root, nullptr);

  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 128, 128, 128, 255);

  rn_view_set_blend_mode(child, "multiply");
  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 128, 0, 0, 255);

  rn_view_set_blend_mode(child, nullptr);
  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 128, 128, 128, 255);
}

// The clip still clips. This is the case the implementation had to be careful
// about: the pops that close each blend happen between the children, so one clip
// around the whole lot would be what those pops closed. Each child gets its own
// clip instead, and the picture has to be the same picture.
TEST(gtk_paint_a_blended_child_is_still_clipped_by_overflow_hidden) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *parent = tree.colouredBox(root, 2, 10, 10, 80, 80, 1.0F, 0.0F, 0.0F);
  rn_view_set_clips_children(parent, TRUE);
  RnView *child = tree.colouredBox(parent, 3, 40, 40, 60, 60, 0.5F, 0.5F, 0.5F);
  rn_view_set_blend_mode(child, "multiply");

  const RnPixels pixels = renderView(root, 100, 100);
  // Inside the parent, where the child is: blended.
  EXPECT_PIXEL(pixels, 70, 70, 128, 0, 0, 255);
  // Past the parent's edge, where the child would reach if nothing clipped it.
  EXPECT_TRANSPARENT(pixels, 95, 95);
}

// zIndex decides the backdrop, because it decides what is painted beneath. The
// blended child here paints first, so the blue sibling is above it rather than
// under it, and the blend is against the parent alone.
TEST(gtk_paint_z_index_decides_what_a_blend_blends_with) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *parent = tree.colouredBox(root, 2, 10, 10, 80, 80, 1.0F, 0.0F, 0.0F);
  RnView *blended = tree.colouredBox(parent, 3, 20, 20, 40, 40, 0.5F, 0.5F, 0.5F);
  rn_view_set_blend_mode(blended, "multiply");
  RnView *above = tree.colouredBox(parent, 4, 20, 20, 20, 20, 0.0F, 0.0F, 1.0F);
  // The blue one is second in the list already; this says so out loud and moves
  // the blended one up instead, so the two orders are not the same order.
  rn_view_set_z_index(above, 1);
  rn_view_set_z_index(blended, 2);

  const RnPixels pixels = renderView(root, 100, 100);
  // Where only the blended child is: half red, as ever.
  EXPECT_PIXEL(pixels, 55, 55, 128, 0, 0, 255);
  // Where both are, the blended one is now on top of the blue one, so the blue
  // one is its backdrop: half blue.
  EXPECT_PIXEL(pixels, 40, 40, 0, 0, 128, 255);
}

// `plus-lighter`, the one CSS mode GSK has no node for. The keyword is still
// stored and still reported -- the dump says what the app asked for -- and the
// view paints unblended rather than wrongly blended. Measured rather than
// assumed, which is the point of asserting it.
TEST(gtk_paint_plus_lighter_is_reported_and_not_blended) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *child = greyOnRed(tree, root, "plus-lighter");

  EXPECT(g_strcmp0(rn_view_get_blend_mode(child), "plus-lighter") == 0);
  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 128, 128, 128, 255);

  char *text = rn_view_describe_tree(child);
  const std::string described(text);
  g_free(text);
  EXPECT(described.find("blend=plus-lighter") != std::string::npos);
}

// And the keyword is in the dump, spelled as the AppKit side spells it, which is
// what lets one scenario check the wiring on both hosts.
TEST(gtk_paint_a_blend_mode_is_reported_in_the_tree) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *child = greyOnRed(tree, root, nullptr);

  char *text = rn_view_describe_tree(child);
  const std::string none(text);
  g_free(text);
  EXPECT(none.find("blend=") == std::string::npos);

  rn_view_set_blend_mode(child, "color-dodge");
  text = rn_view_describe_tree(child);
  const std::string dodging(text);
  g_free(text);
  EXPECT(dodging.find("blend=color-dodge") != std::string::npos);
}

// Two blended children, in an order zIndex reversed. Each blends with what is
// beneath it where it lands, so the two orders give two different pixels -- and
// the pushes have to be made in paint order rather than in the order the
// children were inserted, which is what this is really asserting.
TEST(gtk_paint_two_blends_nest_in_paint_order) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *parent = tree.colouredBox(root, 2, 10, 10, 80, 80, 1.0F, 0.0F, 0.0F);

  RnView *grey = tree.colouredBox(parent, 3, 20, 20, 40, 40, 0.5F, 0.5F, 0.5F);
  rn_view_set_blend_mode(grey, "multiply");
  rn_view_set_z_index(grey, 2);
  RnView *blue = tree.colouredBox(parent, 4, 20, 20, 40, 40, 0.0F, 0.0F, 1.0F);
  rn_view_set_blend_mode(blue, "screen");
  rn_view_set_z_index(blue, 1);

  // Blue first: screen of red and blue is magenta. Then grey: multiply of
  // magenta and half grey is half magenta. The other order ends in (128,0,255),
  // so the blue channel is what says which happened.
  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 128, 0, 128, 255);
}

// A blended child with nothing beneath it, which is the case the blend node's
// two children make worth asking about: the backdrop is empty, so there is
// nothing for GSK to blend against.
//
// CSS answers this exactly -- the result is weighted by the backdrop's alpha, so
// a transparent backdrop leaves the source colour untouched -- and the child has
// to be drawn either way. A blend node built with a missing bottom child and
// dropped on the floor would lose the child entirely, which is the failure this
// is here to catch.
TEST(gtk_paint_a_blend_over_nothing_still_draws_the_child) {
  Tree tree;
  RnView *root = tree.root(1, 100, 100);
  RnView *parent = tree.box(root, 2, 10, 10, 80, 80);
  RnView *child = tree.colouredBox(parent, 3, 20, 20, 40, 40, 0.5F, 0.5F, 0.5F);
  rn_view_set_blend_mode(child, "multiply");

  // The child, unchanged: there was no backdrop to multiply with.
  EXPECT_PIXEL(renderView(root, 100, 100), 50, 50, 128, 128, 128, 255);
  // And nothing where the child is not.
  EXPECT_TRANSPARENT(renderView(root, 100, 100), 20, 20);
}
