// What actually reached the pixels.
//
// Every other suite in this project asserts on the view tree: that a view has a
// colour, a frame and a transform. `docs/BACKLOG.md` has carried the gap that
// leaves since GTK -- "GTK's cairo renderer mangled every transform in the demo
// and no test noticed" -- because closing it on the other two platforms means a
// display server or an offscreen window and a display cycle.
//
// Direct2D renders into a WIC bitmap with no window and no display, so here it
// costs a function call. These are the assertions that would have caught that
// bug: a transform composed in the wrong order, a clip that does not clip, an
// opacity applied per-brush instead of per-subtree, and a zIndex that reorders
// nothing all leave the tree dump identical and the picture wrong.

#include "TestHarness.h"

#include "RnWin32View.h"
#include "Win32Snapshot.h"

#include <d2d1.h>
#include <wrl/client.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using basalt::win32::RnPixel;
using basalt::win32::RnPixels;
using basalt::win32::RnWin32View;

namespace {

// Rounding through premultiplied storage costs a unit or two, and Direct2D
// antialiases edges -- so every point these tests ask about is well inside or
// well outside a shape, never on its boundary.
constexpr double kTolerance = 3.0;

class Tree {
 public:
  RnWin32View *box(int32_t tag, float x, float y, float w, float h) {
    auto view = std::make_unique<RnWin32View>(tag);
    view->setFrame(x, y, w, h);
    RnWin32View *raw = view.get();
    views_.push_back(std::move(view));
    return raw;
  }

  RnWin32View *
  colouredBox(int32_t tag, float x, float y, float w, float h, float r, float g, float b) {
    RnWin32View *view = box(tag, x, y, w, h);
    view->setBackgroundColor(r, g, b, 1.0f, true);
    return view;
  }

 private:
  std::vector<std::unique_ptr<RnWin32View>> views_;
};

// Stands in for the Skia surface behind a `<Canvas>`: fills the box it is
// handed, and remembers what it was handed.
//
// A fake rather than the real thing because the seam is what these tests are
// about. Whether Skia drew the right picture is Skia's business; whether this
// host puts what it drew in the right place, inside the clip and under the
// opacity, is the part that is this host's and the part that has been wrong on
// every platform at least once.
class FillPainter final : public basalt::win32::RnWin32Painter {
 public:
  FillPainter(float red, float green, float blue) : red_(red), green_(green), blue_(blue) {}

  void draw(ID2D1RenderTarget *target, float boxWidth, float boxHeight) override {
    calls++;
    lastWidth = boxWidth;
    lastHeight = boxHeight;
    Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(red_, green_, blue_, 1.0f),
                                             brush.GetAddressOf()))) {
      return;
    }
    target->FillRectangle(D2D1::RectF(0.0f, 0.0f, boxWidth, boxHeight), brush.Get());
  }

  int calls = 0;
  float lastWidth = 0.0f;
  float lastHeight = 0.0f;

 private:
  float red_;
  float green_;
  float blue_;
};

} // namespace

// A macro rather than a function so a failure reports the line that asked,
// which is the only thing that makes a wrong pixel findable.
#define EXPECT_PIXEL(pixels, x, y, r, g, b, a)                                                     \
  do {                                                                                             \
    const RnPixel pixel = (pixels).at((x), (y));                                                   \
    EXPECT_NEAR(pixel.red, (r), kTolerance);                                                       \
    EXPECT_NEAR(pixel.green, (g), kTolerance);                                                     \
    EXPECT_NEAR(pixel.blue, (b), kTolerance);                                                      \
    EXPECT_NEAR(pixel.alpha, (a), kTolerance);                                                     \
  } while (false)

#define EXPECT_TRANSPARENT(pixels, x, y) EXPECT_NEAR((pixels).at((x), (y)).alpha, 0, kTolerance)

TEST(win32_paint_puts_a_child_at_its_frame) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  root->insertChild(tree.colouredBox(2, 20, 30, 40, 10, 1.0f, 0.0f, 0.0f), 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  EXPECT_PIXEL(pixels, 25, 35, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(pixels, 5, 5);
  EXPECT_TRANSPARENT(pixels, 65, 35);
  EXPECT_TRANSPARENT(pixels, 25, 45);
}

TEST(win32_paint_composites_opacity_over_the_whole_subtree) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 60, 60);
  RnWin32View *faded = tree.colouredBox(2, 0, 0, 60, 60, 1.0f, 0.0f, 0.0f);
  faded->setOpacity(0.4f);
  root->insertChild(faded, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  // Still fully red, at 40% coverage. An opacity folded into the brush's alpha
  // would look the same here and wrong the moment two children overlap, which
  // is what the layer in RnWin32View::paint is for.
  EXPECT_PIXEL(pixels, 30, 30, 255, 0, 0, 102);
}

TEST(win32_paint_clips_children_only_when_overflow_is_hidden) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *parent = tree.box(2, 0, 0, 50, 50);
  parent->insertChild(tree.colouredBox(3, 0, 0, 100, 100, 0.0f, 1.0f, 0.0f), 0);
  root->insertChild(parent, 0);

  const RnPixels visible = basalt::win32::renderToPixels(*root);
  EXPECT(!visible.empty());
  EXPECT_PIXEL(visible, 25, 25, 0, 255, 0, 255);
  // overflow: visible is the default, so the child escapes its parent's box.
  EXPECT_PIXEL(visible, 75, 25, 0, 255, 0, 255);

  parent->setClipsChildren(true);
  const RnPixels clipped = basalt::win32::renderToPixels(*root);
  EXPECT(!clipped.empty());
  EXPECT_PIXEL(clipped, 25, 25, 0, 255, 0, 255);
  EXPECT_TRANSPARENT(clipped, 75, 25);
}

TEST(win32_paint_rotates_about_the_centre_and_in_the_right_direction) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  // Deliberately wide and short, so a quarter turn changes which pixels are
  // covered rather than only which way up they are.
  RnWin32View *card = tree.colouredBox(2, 10, 40, 80, 20, 0.0f, 0.0f, 1.0f);
  // A marker in the card's top-left corner. Without it a quarter turn and a
  // three-quarter turn are indistinguishable, because the card is symmetric
  // under a half turn -- and the difference between them is exactly the
  // transpose that no test on an axis-aligned box can see.
  card->insertChild(tree.colouredBox(3, 0, 0, 10, 10, 1.0f, 1.0f, 1.0f), 0);
  root->insertChild(card, 0);

  const RnPixels upright = basalt::win32::renderToPixels(*root);
  EXPECT(!upright.empty());
  // The card spans x 10..90, y 40..60; the marker is its corner, x 10..20,
  // y 40..50.
  EXPECT_PIXEL(upright, 30, 50, 0, 0, 255, 255);
  EXPECT_PIXEL(upright, 15, 45, 255, 255, 255, 255);
  EXPECT_TRANSPARENT(upright, 50, 20);

  // CSS rotate(90deg), in matrix3d's column-major order: m11=0, m12=1, m21=-1,
  // m22=0. On screen, with y downwards, that turns clockwise.
  const float quarterTurn[16] = {
      0, 1, 0, 0, //
      -1, 0, 0, 0, //
      0, 0, 1, 0, //
      0, 0, 0, 1};
  card->setTransform(quarterTurn);

  const RnPixels turned = basalt::win32::renderToPixels(*root);
  EXPECT(!turned.empty());

  // The card's centre is (50,50) and it is now 20 wide and 80 tall about it, so
  // the pixels it covers have swapped: (50,60) is inside and (20,50) is not.
  EXPECT_PIXEL(turned, 50, 60, 0, 0, 255, 255);
  EXPECT_TRANSPARENT(turned, 20, 50);

  // And the marker has swung to x 50..60, y 10..20 -- clockwise. The transposed
  // matrix, which is the mistake no axis-aligned test can see, would put it at
  // x 40..50, y 80..90 instead. Both ends are asserted, because the card covers
  // both and only the marker's colour tells them apart.
  EXPECT_PIXEL(turned, 55, 15, 255, 255, 255, 255);
  EXPECT_PIXEL(turned, 45, 85, 0, 0, 255, 255);
}

TEST(win32_paint_orders_children_by_z_index) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *under = tree.colouredBox(2, 0, 0, 50, 50, 1.0f, 0.0f, 0.0f);
  RnWin32View *over = tree.colouredBox(3, 0, 0, 50, 50, 0.0f, 1.0f, 0.0f);
  root->insertChild(under, 0);
  root->insertChild(over, 1);

  // Document order: the later child wins.
  const RnPixels ordered = basalt::win32::renderToPixels(*root);
  EXPECT(!ordered.empty());
  EXPECT_PIXEL(ordered, 25, 25, 0, 255, 0, 255);

  // A negative zIndex on the later child puts it underneath, and the child list
  // does not move -- which test_win32_view.cpp asserts separately, because
  // Fabric indexes into that list.
  over->setZIndex(-1);
  const RnPixels restacked = basalt::win32::renderToPixels(*root);
  EXPECT(!restacked.empty());
  EXPECT_PIXEL(restacked, 25, 25, 255, 0, 0, 255);
}

TEST(win32_paint_moves_children_by_the_scroll_offset) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *row = tree.colouredBox(2, 0, 50, 100, 20, 1.0f, 0.0f, 0.0f);
  root->insertChild(row, 0);

  const RnPixels unscrolled = basalt::win32::renderToPixels(*root);
  EXPECT(!unscrolled.empty());
  EXPECT_PIXEL(unscrolled, 50, 55, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(unscrolled, 50, 25);

  // Scrolled down by 30, the row that was at y=50 is drawn at y=20 -- and its
  // frame has not changed, which is the invariant: Yoga decides where things
  // are and the offset is applied at paint time.
  root->setScrollOffset(0, 30);
  const RnPixels scrolled = basalt::win32::renderToPixels(*root);
  EXPECT(!scrolled.empty());
  EXPECT_PIXEL(scrolled, 50, 25, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(scrolled, 50, 55);
  EXPECT_NEAR(row->frame().y, 50.0, 0.001);
}

TEST(win32_paint_rounds_the_background_to_the_corner_radius) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *rounded = tree.colouredBox(2, 0, 0, 100, 100, 1.0f, 0.0f, 0.0f);
  rounded->setCornerRadius(40.0f);
  root->insertChild(rounded, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  // The middle is filled and the corner is not, which is the whole claim. The
  // background is clipped to the radius whether or not children are.
  EXPECT_PIXEL(pixels, 50, 50, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(pixels, 2, 2);
}

// ---------------------------------------------------------------------------
// Per-corner radii and borders
//
// This host drew one circular radius -- the top-left horizontal one -- and no
// border at all, while GTK and AppKit drew both. The tree dump said so, which
// is how scripts/compare_hosts.sh caught it; these say what reached the pixels.
// ---------------------------------------------------------------------------

TEST(win32_paint_rounds_each_corner_on_its_own) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *box = tree.colouredBox(2, 0, 0, 100, 100, 1.0f, 0.0f, 0.0f);
  // Top-left and bottom-right rounded, the other two square: what
  // borderTopLeftRadius and borderBottomRightRadius alone ask for.
  const float radii[8] = {40, 40, 0, 0, 40, 40, 0, 0};
  box->setCornerRadii(radii);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());
  EXPECT_TRANSPARENT(pixels, 3, 3);
  EXPECT_TRANSPARENT(pixels, 96, 96);
  // The two square corners are still there, which one radius for all four
  // could never have produced.
  EXPECT_PIXEL(pixels, 97, 2, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 2, 97, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 50, 50, 255, 0, 0, 255);
}

TEST(win32_paint_rounds_a_corner_elliptically) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *box = tree.colouredBox(2, 0, 0, 100, 100, 1.0f, 0.0f, 0.0f);
  // 60 across and 20 down at the top-left. (15,3) is inside a 20-radius circle
  // and outside this ellipse; (50,15) is inside the ellipse. Only an elliptical
  // corner answers both.
  const float radii[8] = {60, 20, 0, 0, 0, 0, 0, 0};
  box->setCornerRadii(radii);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());
  EXPECT_TRANSPARENT(pixels, 15, 3);
  EXPECT_PIXEL(pixels, 50, 15, 255, 0, 0, 255);
}

TEST(win32_paint_draws_a_border_inside_the_box) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *box = tree.colouredBox(2, 0, 0, 100, 100, 0.0f, 0.0f, 1.0f);
  const float widths[4] = {10, 10, 10, 10};
  const float colours[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  box->setBorders(widths, colours);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());
  // Inside the frame on every side -- the border is part of the box, which is
  // what Yoga already inset the content by -- and the middle untouched.
  EXPECT_PIXEL(pixels, 50, 4, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 95, 50, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 50, 95, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 4, 50, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 50, 50, 0, 0, 255, 255);
}

TEST(win32_paint_colours_each_edge_and_mitres_the_corners) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *box = tree.box(2, 0, 0, 100, 100);
  const float widths[4] = {10, 10, 10, 10};
  // Top red, right green, bottom blue, left yellow.
  const float colours[16] = {1, 0, 0, 1, 0, 1, 0, 1, 0, 0, 1, 1, 1, 1, 0, 1};
  box->setBorders(widths, colours);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());
  EXPECT_PIXEL(pixels, 50, 4, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 95, 50, 0, 255, 0, 255);
  EXPECT_PIXEL(pixels, 50, 95, 0, 0, 255, 255);
  EXPECT_PIXEL(pixels, 4, 50, 255, 255, 0, 255);
  // The top-left corner is split down its diagonal, as CSS splits it: above the
  // line from (0,0) to (10,10) is the top edge's, below it the left edge's.
  EXPECT_PIXEL(pixels, 7, 2, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 2, 7, 255, 255, 0, 255);
  // No background, so the middle is nothing.
  EXPECT_TRANSPARENT(pixels, 50, 50);
}

TEST(win32_paint_draws_the_border_over_the_children) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *parent = tree.box(2, 0, 0, 100, 100);
  const float widths[4] = {10, 10, 10, 10};
  const float colours[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  parent->setBorders(widths, colours);
  root->insertChild(parent, 0);
  // A child covering the whole box, border included: the border still shows,
  // because it is painted after the content on every platform.
  parent->insertChild(tree.colouredBox(3, 0, 0, 100, 100, 0.0f, 1.0f, 0.0f), 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());
  EXPECT_PIXEL(pixels, 50, 4, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 50, 50, 0, 255, 0, 255);
}

TEST(win32_paint_clips_children_to_each_corner) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *parent = tree.box(2, 0, 0, 100, 100);
  const float radii[8] = {40, 40, 0, 0, 0, 0, 0, 0};
  parent->setCornerRadii(radii);
  parent->setClipsChildren(true);
  root->insertChild(parent, 0);
  parent->insertChild(tree.colouredBox(3, 0, 0, 100, 100, 0.0f, 1.0f, 0.0f), 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());
  // overflow: hidden cuts the child at the rounded corner and nowhere else.
  EXPECT_TRANSPARENT(pixels, 3, 3);
  EXPECT_PIXEL(pixels, 97, 3, 0, 255, 0, 255);
  EXPECT_PIXEL(pixels, 3, 97, 0, 255, 0, 255);
}

TEST(win32_describe_prints_radii_and_borders_as_gtk_does) {
  // The line e2e/index.js's header produces on Linux, which is the one the
  // cross-host comparison failed on: two rounded corners, one colour on three
  // edges and another on the fourth.
  RnWin32View view(16);
  view.setFrame(0, 0, 50, 50);
  const float radii[8] = {18, 18, 0, 0, 18, 18, 0, 0};
  view.setCornerRadii(radii);
  const float widths[4] = {4, 4, 4, 4};
  const float gold[4] = {242 / 255.0f, 193 / 255.0f, 78 / 255.0f, 1.0f};
  const float coral[4] = {242 / 255.0f, 111 / 255.0f, 86 / 255.0f, 1.0f};
  float colours[16];
  for (int c = 0; c < 4; c++) {
    colours[0 + c] = gold[c];
    colours[4 + c] = gold[c];
    colours[8 + c] = gold[c];
    colours[12 + c] = coral[c];
  }
  view.setBorders(widths, colours);

  const std::string tree = view.describeTree();
  EXPECT(tree.find(" radii=(18,18,0,0,18,18,0,0)") != std::string::npos);
  EXPECT(tree.find(" borderw=(4,4,4,4) borderc=(#f2c14eff,#f2c14eff,#f2c14eff,#f26f56ff)") !=
         std::string::npos);

  // And a border that cannot be seen is not a border: GTK prints none for it,
  // so neither does this.
  const float clear[16] = {};
  view.setBorders(widths, clear);
  EXPECT(view.describeTree().find("borderw=") == std::string::npos);
}

TEST(win32_paint_gives_a_painter_its_views_box) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *canvas = tree.box(2, 20, 30, 40, 10);
  auto painter = std::make_shared<FillPainter>(1.0f, 0.0f, 0.0f);
  canvas->setPainter(painter);
  root->insertChild(canvas, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  // Once, with the frame Yoga resolved -- not the root's, and not in pixels.
  EXPECT_EQ(painter->calls, 1);
  EXPECT_NEAR(painter->lastWidth, 40.0, 0.01);
  EXPECT_NEAR(painter->lastHeight, 10.0, 0.01);

  // And at the view's origin, so what it drew at 0,0 lands at 20,30.
  EXPECT_PIXEL(pixels, 25, 35, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(pixels, 5, 5);
  EXPECT_TRANSPARENT(pixels, 65, 35);
}

TEST(win32_paint_clips_and_fades_a_painter_with_the_views_above_it) {
  // The whole argument for drawing a <Canvas> in the walk instead of giving it
  // a surface of its own: it is clipped and faded by its ancestors without
  // knowing that any of them exist. A sibling layer would need every one of
  // these reimplemented, which is where the other toolkits' bugs live.
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *clip = tree.box(2, 0, 0, 50, 50);
  clip->setClipsChildren(true);
  clip->setOpacity(0.4f);
  RnWin32View *canvas = tree.box(3, 0, 0, 100, 100);
  canvas->setPainter(std::make_shared<FillPainter>(0.0f, 1.0f, 0.0f));
  clip->insertChild(canvas, 0);
  root->insertChild(clip, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  // Inside the clip: green at 40%.
  EXPECT_PIXEL(pixels, 25, 25, 0, 255, 0, 102);
  // Outside it: nothing, though the painter filled all hundred points.
  EXPECT_TRANSPARENT(pixels, 75, 25);
}

TEST(win32_paint_scrolls_a_painter_with_the_content_around_it) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *scroller = tree.box(2, 0, 0, 100, 100);
  RnWin32View *canvas = tree.box(3, 0, 40, 100, 20);
  canvas->setPainter(std::make_shared<FillPainter>(0.0f, 0.0f, 1.0f));
  scroller->insertChild(canvas, 0);
  root->insertChild(scroller, 0);

  const RnPixels still = basalt::win32::renderToPixels(*root);
  EXPECT(!still.empty());
  EXPECT_PIXEL(still, 50, 45, 0, 0, 255, 255);

  scroller->setScrollOffset(0, 30);
  const RnPixels scrolled = basalt::win32::renderToPixels(*root);
  EXPECT(!scrolled.empty());
  // Up by thirty: the painter is not told, and does not need to be.
  EXPECT_PIXEL(scrolled, 50, 15, 0, 0, 255, 255);
  EXPECT_TRANSPARENT(scrolled, 50, 45);
}

TEST(win32_paint_skips_a_painter_on_a_hidden_view) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *canvas = tree.box(2, 0, 0, 100, 100);
  auto painter = std::make_shared<FillPainter>(1.0f, 1.0f, 0.0f);
  canvas->setPainter(painter);
  canvas->setHidden(true);
  root->insertChild(canvas, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  // `display: none` reaches the painter as not being called at all, which for a
  // canvas is the difference between idle and rendering a frame nobody sees.
  EXPECT_EQ(painter->calls, 0);
  EXPECT_TRANSPARENT(pixels, 50, 50);
}

// ---------------------------------------------------------------------------
// CSS's outline: `outlineWidth`, `outlineColor`, `outlineOffset` and
// `outlineStyle`.
//
// The same five questions tests/test_gtk_paint.cpp asks, in the same order and
// with the same geometry, because the ring has to land in the same place on
// both: it is drawn *outside* the box and takes no layout space, so what proves
// it is ink beyond the view's own frame with the frame unchanged. Web-ported
// code sets it for a focus ring and expects exactly that.
// ---------------------------------------------------------------------------

TEST(win32_paint_draws_an_outline_outside_the_box) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *view = tree.colouredBox(2, 30, 30, 40, 40, 1.0f, 1.0f, 1.0f);
  root->insertChild(view, 0);
  const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  view->setOutline(4.0f, 0.0f, red, RnWin32View::LineStyle::Solid);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());
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
TEST(win32_paint_an_outline_offset_leaves_a_gap) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *view = tree.colouredBox(2, 30, 30, 40, 40, 1.0f, 1.0f, 1.0f);
  root->insertChild(view, 0);
  const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  view->setOutline(4.0f, 6.0f, red, RnWin32View::LineStyle::Solid);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  // The gap: three points out from the edge, between the box and the ring.
  EXPECT_TRANSPARENT(pixels, 27, 50);
  // The ring itself, eight points out.
  EXPECT_PIXEL(pixels, 22, 50, 255, 0, 0, 255);
  // And past it again.
  EXPECT_TRANSPARENT(pixels, 16, 50);
}

// A dashed outline leaves gaps along the ring, the way a dashed border does.
// Direct2D's dash lengths are multiples of the stroke width, where the other two
// hosts' arrays are absolute, so this is the test that would catch a pattern
// converted into the wrong units: four times too long is a ring with no gaps.
TEST(win32_paint_a_dashed_outline_is_not_solid) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *view = tree.colouredBox(2, 20, 20, 60, 60, 1.0f, 1.0f, 1.0f);
  root->insertChild(view, 0);
  const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};

  view->setOutline(4.0f, 0.0f, red, RnWin32View::LineStyle::Solid);
  const RnPixels solid = basalt::win32::renderToPixels(*root);
  int solidInk = 0;
  for (unsigned x = 0; x < solid.width(); x++) {
    if (solid.at(x, 18).alpha > 40) {
      solidInk++;
    }
  }

  view->setOutline(4.0f, 0.0f, red, RnWin32View::LineStyle::Dashed);
  const RnPixels dashed = basalt::win32::renderToPixels(*root);
  int dashedInk = 0;
  for (unsigned x = 0; x < dashed.width(); x++) {
    if (dashed.at(x, 18).alpha > 40) {
      dashedInk++;
    }
  }

  // Ink either way, and less of it when it is dashed.
  EXPECT(solidInk > 50);
  EXPECT(dashedInk > 0);
  EXPECT(dashedInk < solidInk);
}

TEST(win32_paint_no_outline_width_is_no_outline) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *view = tree.colouredBox(2, 30, 30, 40, 40, 1.0f, 1.0f, 1.0f);
  root->insertChild(view, 0);
  const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};

  view->setOutline(4.0f, 0.0f, red, RnWin32View::LineStyle::Solid);
  EXPECT_PIXEL(basalt::win32::renderToPixels(*root), 28, 50, 255, 0, 0, 255);

  // Taken away again, which is what losing focus does.
  view->setOutline(0.0f, 0.0f, red, RnWin32View::LineStyle::Solid);
  EXPECT_TRANSPARENT(basalt::win32::renderToPixels(*root), 28, 50);

  // And a width with no colour paints nothing: React Native's default outline
  // colour is undefined, which arrives as a transparent one.
  const float invisible[4] = {0.0f, 0.0f, 0.0f, 0.0f};
  view->setOutline(4.0f, 0.0f, invisible, RnWin32View::LineStyle::Solid);
  EXPECT_TRANSPARENT(basalt::win32::renderToPixels(*root), 28, 50);
}

// The ring follows the view's corners, grown by the width and the offset so it
// stays concentric: a square ring around a rounded card is the giveaway that the
// radii were not carried over.
TEST(win32_paint_an_outline_follows_the_corner_radius) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *view = tree.colouredBox(2, 20, 20, 60, 60, 1.0f, 1.0f, 1.0f);
  root->insertChild(view, 0);
  const float radii[8] = {20, 20, 20, 20, 20, 20, 20, 20};
  view->setCornerRadii(radii);
  const float red[4] = {1.0f, 0.0f, 0.0f, 1.0f};
  view->setOutline(4.0f, 0.0f, red, RnWin32View::LineStyle::Solid);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  // The middle of an edge is on the ring.
  EXPECT_PIXEL(pixels, 18, 50, 255, 0, 0, 255);
  // The corner of the bounding box is outside a 22 point radius, so it is
  // empty -- where a square ring would paint it.
  EXPECT_TRANSPARENT(pixels, 17, 17);
}

// And all four numbers are in the dump, spelled as the other two hosts spell
// them, because that line is what the end-to-end scenario reads on all three.
TEST(win32_describe_prints_the_outline_as_gtk_does) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *view = tree.box(2, 10, 10, 40, 40);
  root->insertChild(view, 0);

  EXPECT(root->describeTree().find("outline=") == std::string::npos);

  const float colour[4] = {224.0f / 255.0f, 72.0f / 255.0f, 77.0f / 255.0f, 1.0f};
  view->setOutline(3.0f, 2.0f, colour, RnWin32View::LineStyle::Dashed);
  EXPECT(root->describeTree().find("outline=(3,2,#e0484dff,dashed)") != std::string::npos);

  // Solid prints no style, which is what keeps the common case short and is
  // what the other two hosts do.
  view->setOutline(3.0f, 2.0f, colour, RnWin32View::LineStyle::Solid);
  EXPECT(root->describeTree().find("outline=(3,2,#e0484dff)") != std::string::npos);
}

// ---------------------------------------------------------------------------
// `borderStyle`, dotted and dashed.
//
// One stroked path around the rounded box rather than four filled edges, which
// is what both other hosts do and why the style belongs to the whole border:
// a stroked path carries one dash pattern.
// ---------------------------------------------------------------------------

TEST(win32_paint_a_dashed_border_is_not_solid) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *box = tree.box(2, 0, 0, 100, 100);
  const float widths[4] = {4, 4, 4, 4};
  const float colours[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  box->setBorders(widths, colours);
  root->insertChild(box, 0);

  const auto inkAlongTheTop = [&]() {
    const RnPixels pixels = basalt::win32::renderToPixels(*root);
    int ink = 0;
    for (unsigned x = 0; x < pixels.width(); x++) {
      if (pixels.at(x, 2).alpha > 40) {
        ink++;
      }
    }
    return ink;
  };

  const int solid = inkAlongTheTop();
  box->setBorderStyle(RnWin32View::LineStyle::Dashed);
  const int dashed = inkAlongTheTop();
  box->setBorderStyle(RnWin32View::LineStyle::Dotted);
  const int dotted = inkAlongTheTop();

  // Ink in all three, and less of it once there are gaps.
  EXPECT(solid > 80);
  EXPECT(dashed > 0);
  EXPECT(dashed < solid);
  EXPECT(dotted > 0);
  EXPECT(dotted < solid);
  // Dots leave more gap than dashes: three widths on and two off against zero
  // on and two off. A pattern converted into the wrong units would make these
  // two the same, or make both solid.
  EXPECT(dotted < dashed);
}

// The stroke stays inside the box, which is the half that a straddling stroke
// gets wrong: a 4pt dashed border drawn on the box's own edge would paint two
// points outside the view and overlap its neighbour.
TEST(win32_paint_a_dashed_border_stays_inside_the_box) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *box = tree.box(2, 20, 20, 60, 60);
  const float widths[4] = {4, 4, 4, 4};
  const float colours[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  box->setBorders(widths, colours);
  box->setBorderStyle(RnWin32View::LineStyle::Dashed);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  // Nothing outside the frame on any side. A dash can fall anywhere along the
  // path, so this counts a band rather than asking about one pixel.
  int outside = 0;
  for (unsigned x = 0; x < pixels.width(); x++) {
    for (unsigned y = 0; y < pixels.height(); y++) {
      const bool insideFrame = x >= 20 && x < 80 && y >= 20 && y < 80;
      if (!insideFrame && pixels.at(x, y).alpha > 40) {
        outside++;
      }
    }
  }
  EXPECT_EQ(outside, 0);
}

TEST(win32_describe_prints_the_border_style_as_gtk_does) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *box = tree.box(2, 10, 10, 40, 40);
  const float widths[4] = {2, 2, 2, 2};
  const float colours[16] = {1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 1};
  box->setBorders(widths, colours);
  root->insertChild(box, 0);

  // Solid prints nothing, which keeps the common case short and is what the
  // other two hosts do.
  EXPECT(root->describeTree().find("border-style=") == std::string::npos);

  box->setBorderStyle(RnWin32View::LineStyle::Dashed);
  EXPECT(root->describeTree().find("border-style=dashed") != std::string::npos);
  box->setBorderStyle(RnWin32View::LineStyle::Dotted);
  EXPECT(root->describeTree().find("border-style=dotted") != std::string::npos);
}

// ---------------------------------------------------------------------------
// `filter`, which is an effect graph over the view's own picture.
//
// CSS applies a filter to an element and its descendants, so the assertions
// below are about what reached the pixels after the subtree was drawn: a colour
// matrix that recolours a child as well as its parent, a blur that spreads an
// edge, an opacity that multiplies the view's own, and a drop shadow cast by
// the result.
//
// The matrix test is deliberately asymmetric. Direct2D multiplies a row vector
// by its matrix where core's rows are outputs, so the matrix goes in
// transposed: a symmetric one would pass either way, and a channel rotation
// comes out blue when it is right and green when it is not.
// ---------------------------------------------------------------------------
namespace {

RnWin32View::Filters rotateChannels() {
  // out_red = in_green, out_green = in_blue, out_blue = in_red.
  RnWin32View::Filters filters;
  filters.hasMatrix = true;
  for (int i = 0; i < 16; i++) {
    filters.matrix[i] = 0.0f;
  }
  filters.matrix[0 * 4 + 1] = 1.0f;
  filters.matrix[1 * 4 + 2] = 1.0f;
  filters.matrix[2 * 4 + 0] = 1.0f;
  filters.matrix[3 * 4 + 3] = 1.0f;
  return filters;
}

} // namespace

TEST(win32_paint_applies_a_filters_colour_matrix) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 60, 60);
  RnWin32View *box = tree.colouredBox(2, 0, 0, 60, 60, 1.0f, 0.0f, 0.0f);
  box->setFilters(rotateChannels());
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());
  // Red in, blue out. Green out would mean the matrix went in untransposed,
  // which is the one mistake this arrangement cannot hide.
  EXPECT_PIXEL(pixels, 30, 30, 0, 0, 255, 255);
}

TEST(win32_paint_applies_a_filter_to_the_whole_subtree) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 60, 60);
  RnWin32View *parent = tree.box(2, 0, 0, 60, 60);
  parent->setFilters(rotateChannels());
  RnWin32View *child = tree.colouredBox(3, 10, 10, 40, 40, 1.0f, 0.0f, 0.0f);
  parent->insertChild(child, 0);
  root->insertChild(parent, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  // The child is red and is inside a filtered parent, so it comes out blue:
  // that is what "and its descendants" means, and a host that filtered only
  // its own background would leave this red.
  EXPECT_PIXEL(pixels, 30, 30, 0, 0, 255, 255);
}

TEST(win32_paint_applies_a_filters_matrix_offset) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 60, 60);
  RnWin32View *box = tree.colouredBox(2, 0, 0, 60, 60, 0.0f, 0.0f, 0.0f);
  RnWin32View::Filters filters;
  filters.hasMatrix = true;
  // Identity, plus all the green there is: `out = matrix * in + offset`, which
  // is the fifth row of Direct2D's matrix rather than a fifth column.
  filters.offset[1] = 1.0f;
  box->setFilters(filters);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT_PIXEL(pixels, 30, 30, 0, 255, 0, 255);
}

// The alpha mode, which is the other thing a colour matrix can get wrong.
//
// CSS defines its filters on unpremultiplied colour and Direct2D's default is
// premultiplied, so a matrix over a half-transparent view is visibly wrong the
// other way round: the red channel of a 50% red pixel is 0.5 premultiplied and
// 1.0 straight.
TEST(win32_paint_filters_in_straight_alpha) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 60, 60);
  RnWin32View *box = tree.box(2, 0, 0, 60, 60);
  box->setBackgroundColor(1.0f, 0.0f, 0.0f, 0.5f, true);
  RnWin32View::Filters filters;
  filters.hasMatrix = true;
  // Identity on colour, and alpha forced opaque: row three of the matrix is
  // zeroed and the offset supplies the one.
  for (int column = 0; column < 4; column++) {
    filters.matrix[3 * 4 + column] = 0.0f;
  }
  filters.offset[3] = 1.0f;
  box->setFilters(filters);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  // Full red at full alpha. Premultiplied would give a dark red, about 128.
  EXPECT_PIXEL(pixels, 30, 30, 255, 0, 0, 255);
}

TEST(win32_paint_applies_a_filters_blur) {
  const auto edgeRamp = [](float radius) {
    Tree tree;
    RnWin32View *root = tree.box(1, 0, 0, 100, 40);
    RnWin32View *parent = tree.box(2, 0, 0, 100, 40);
    RnWin32View::Filters filters;
    filters.blurRadius = radius;
    parent->setFilters(filters);
    // A hard edge down the middle: black on the left, nothing on the right.
    RnWin32View *half = tree.colouredBox(3, 0, 0, 50, 40, 0.0f, 0.0f, 0.0f);
    parent->insertChild(half, 0);
    root->insertChild(parent, 0);

    const RnPixels pixels = basalt::win32::renderToPixels(*root);
    int mixed = 0;
    for (unsigned x = 0; x < pixels.width(); x++) {
      const int alpha = pixels.at(x, 20).alpha;
      if (alpha > 24 && alpha < 232) {
        mixed++;
      }
    }
    return mixed;
  };

  const int sharp = edgeRamp(0.0f);
  const int blurred = edgeRamp(10.0f);
  EXPECT(sharp <= 4);
  EXPECT(blurred > sharp + 5);
}

TEST(win32_paint_multiplies_the_views_opacity_by_the_filters) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 60, 60);
  RnWin32View *box = tree.colouredBox(2, 0, 0, 60, 60, 0.0f, 0.0f, 0.0f);
  box->setOpacity(0.5f);
  RnWin32View::Filters filters;
  filters.opacity = 0.5f;
  box->setFilters(filters);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  // CSS has two ways to ask for the same thing and an app can use both, so a
  // quarter rather than a half.
  EXPECT_NEAR(pixels.at(30, 30).alpha, 64, 6);
}

TEST(win32_paint_casts_a_filters_drop_shadow) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 100, 100);
  RnWin32View *box = tree.colouredBox(2, 20, 20, 40, 40, 1.0f, 1.0f, 1.0f);
  RnWin32View::Filters filters;
  RnWin32View::FilterShadow shadow;
  shadow.dx = 12.0f;
  shadow.dy = 12.0f;
  shadow.standardDeviation = 0.0f;
  shadow.colour[0] = 0.0f;
  shadow.colour[1] = 0.0f;
  shadow.colour[2] = 0.0f;
  shadow.colour[3] = 1.0f;
  filters.shadows.push_back(shadow);
  box->setFilters(filters);
  root->insertChild(box, 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  // Down and right of the box, where the shadow landed: black.
  EXPECT_PIXEL(pixels, 65, 65, 0, 0, 0, 255);
  // The box itself is still white, so the shadow is behind it rather than over
  // it.
  EXPECT_PIXEL(pixels, 30, 30, 255, 255, 255, 255);
  // And up and left of the box there is nothing: a shadow with an offset does
  // not surround its caster.
  EXPECT_TRANSPARENT(pixels, 10, 10);
}

TEST(win32_describe_prints_a_filter_as_gtk_does) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 60, 60);
  RnWin32View *box = tree.box(2, 0, 0, 60, 60);
  root->insertChild(box, 0);

  EXPECT(root->describeTree().find("filter=") == std::string::npos);

  // The probe is (1, 0.5, 0.25) and the matrix rotates the channels, so the
  // answer is each channel's neighbour: green, blue, red.
  box->setFilters(rotateChannels());
  EXPECT(root->describeTree().find("filter=(probe=#8040ffff)") != std::string::npos);

  RnWin32View::Filters pieces;
  pieces.blurRadius = 8.0f;
  pieces.opacity = 0.5f;
  RnWin32View::FilterShadow shadow;
  shadow.dx = 4.0f;
  shadow.dy = 6.0f;
  shadow.standardDeviation = 3.0f;
  shadow.colour[3] = 0.5f;
  pieces.shadows.push_back(shadow);
  box->setFilters(pieces);
  const std::string dump = root->describeTree();
  EXPECT(dump.find("blur=8") != std::string::npos);
  EXPECT(dump.find("opacity=0.5") != std::string::npos);
  // The standard deviation React Native parsed, which is the number all three
  // hosts are handed.
  EXPECT(dump.find("shadow=(4,6,3,#00000080)") != std::string::npos);
}
