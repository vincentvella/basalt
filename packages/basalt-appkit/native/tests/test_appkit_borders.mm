// Tests for what a border is drawn as: four filled edges, or one dashed stroke.
//
// Against a real bitmap rather than against the properties the view stored, for
// the reason the GTK side walks its render tree: `borderStyle` is a prop that is
// easy to store and forget to draw, and a view that remembered "dashed" and
// painted a solid line would pass any test that asked it what it holds.
//
// What is counted is runs of ink along the top edge. A solid border is one run
// from corner to corner; a dashed one is several, and a dotted one is more of
// them than a dashed one at the same width, because the pattern is scaled to the
// width in both cases and a dot's period is shorter.

#include "TestHarness.h"

#import "RnAppKitView.h"

#include <sstream>
#include <vector>

namespace {

// The red channel of the view drawn onto white, row-major.
std::vector<unsigned char> drawnPixels(RnAppKitView *view, CGSize size) {
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height,
                                               8, 0, space, kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(context, 1, 1, 1, 1);
  CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
  CGContextTranslateCTM(context, 0, size.height);
  CGContextScaleCTM(context, 1, -1);

  NSGraphicsContext *previous = NSGraphicsContext.currentContext;
  NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context
                                                                             flipped:YES];
  [view drawRect:NSMakeRect(0, 0, size.width, size.height)];
  NSGraphicsContext.currentContext = previous;

  auto *bytes = static_cast<unsigned char *>(CGBitmapContextGetData(context));
  const size_t stride = CGBitmapContextGetBytesPerRow(context);
  std::vector<unsigned char> red((size_t)size.width * (size_t)size.height);
  for (size_t y = 0; y < (size_t)size.height; y++) {
    for (size_t x = 0; x < (size_t)size.width; x++) {
      red[y * (size_t)size.width + x] = bytes[y * stride + x * 4];
    }
  }
  CGContextRelease(context);
  return red;
}

// How many separate runs of ink there are along one row, and how much of the row
// is inked at all. One run spanning the width is a solid line.
struct Runs {
  int count{0};
  int inked{0};
};

Runs runsAlong(RnAppKitView *view, CGSize size, int row) {
  const std::vector<unsigned char> pixels = drawnPixels(view, size);
  Runs runs;
  bool inside = false;
  for (int x = 0; x < (int)size.width; x++) {
    const bool ink = pixels[(size_t)row * (size_t)size.width + (size_t)x] < 128;
    if (ink) {
      runs.inked++;
      if (!inside) {
        runs.count++;
      }
    }
    inside = ink;
  }
  return runs;
}

RnAppKitView *bordered(CGFloat width, RnAppKitBorderStyle style, CGSize size) {
  RnAppKitView *view = [RnAppKitView viewWithTag:1];
  [view setRnFrameX:0 y:0 width:size.width height:size.height];
  const CGFloat widths[4] = {width, width, width, width};
  CGFloat colors[16];
  for (int edge = 0; edge < 4; edge++) {
    colors[edge * 4 + 0] = 0;
    colors[edge * 4 + 1] = 0;
    colors[edge * 4 + 2] = 0;
    colors[edge * 4 + 3] = 1;
  }
  [view setRnBorderWidths:widths colors:colors];
  [view setRnBorderStyle:style];
  return view;
}

} // namespace

TEST(border_solid_is_one_unbroken_line) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 60);
    const Runs runs = runsAlong(bordered(4, RnAppKitBorderStyleSolid, size), size, 2);
    EXPECT_EQ(runs.count, 1);
    EXPECT_EQ(runs.inked, 100);
  }
}

TEST(border_dashed_leaves_gaps) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 60);
    const Runs runs = runsAlong(bordered(4, RnAppKitBorderStyleDashed, size), size, 2);
    // 12 on, 8 off at this width, so five dashes across a hundred points. What
    // is asserted is that there is more than one and that the row is not full:
    // the exact count depends on where the path starts its phase, which is not
    // worth pinning.
    EXPECT(runs.count > 2);
    EXPECT(runs.inked < 100);
  }
}

// Dotted is not just dashed with a different comment: the pattern is shorter, so
// there are more runs along the same edge at the same width.
TEST(border_dotted_is_denser_than_dashed) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 60);
    const Runs dashed = runsAlong(bordered(4, RnAppKitBorderStyleDashed, size), size, 2);
    const Runs dotted = runsAlong(bordered(4, RnAppKitBorderStyleDotted, size), size, 2);
    EXPECT(dotted.count > dashed.count);
    // And a dot is round rather than a stretch of line, so less of the row is
    // inked than the dashes ink.
    EXPECT(dotted.inked < dashed.inked);
  }
}

// The stroke replaces the four filled edges rather than joining them. Two would
// paint the outline twice and leave the dashes sitting on a solid line, which
// looks like a solid border and is the mistake this guards.
TEST(border_dashed_does_not_also_paint_the_solid_edges) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 60);
    // The row through the middle of the left edge's band, where a filled left
    // edge would be continuous from top to bottom.
    const std::vector<unsigned char> pixels =
        drawnPixels(bordered(4, RnAppKitBorderStyleDashed, size), size);
    int inked = 0;
    for (int y = 0; y < (int)size.height; y++) {
      if (pixels[(size_t)y * (size_t)size.width + 2] < 128) {
        inked++;
      }
    }
    EXPECT(inked > 0);
    EXPECT(inked < (int)size.height);
  }
}

// A style with no width is not a border. Stroking here would put a line around
// every view that mentioned `borderStyle` and set no `borderWidth`.
TEST(border_a_style_with_no_width_draws_nothing) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 60);
    RnAppKitView *view = [RnAppKitView viewWithTag:1];
    [view setRnFrameX:0 y:0 width:size.width height:size.height];
    [view setRnBorderStyle:RnAppKitBorderStyleDashed];
    EXPECT_EQ(runsAlong(view, size, 2).inked, 0);
  }
}

// And the style is in the tree dump, which is what makes the wiring testable:
// the prop is read in AppKitMountingManager, and every test above sets it on the
// view by hand. Spelled as the GTK side spells it, so the cross-host diff can
// compare them.
TEST(border_style_is_reported_in_the_tree) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 60);
    EXPECT(![[bordered(4, RnAppKitBorderStyleSolid, size) describeTree]
        containsString:@"border-style="]);
    EXPECT([[bordered(4, RnAppKitBorderStyleDashed, size) describeTree]
        containsString:@"border-style=dashed"]);
    EXPECT([[bordered(4, RnAppKitBorderStyleDotted, size) describeTree]
        containsString:@"border-style=dotted"]);
  }
}

// `boxShadow`.
//
// Two things are checked, because they fail differently: what each shadow's layer
// carries -- the path, the blur, the colour -- and where that layer lives, which
// is what decides whether `overflow: 'hidden'` clips the shadow away.
//
// **No pixels here, and that is measured rather than assumed.** A layer's shadow
// is drawn by Core Animation during compositing; `-[CALayer renderInContext:]`
// does not draw it at all, which a probe confirmed on a bare layer with a
// shadowPath and nothing else -- every pixel came back white, inside the path as
// well as outside. That is also the method `AppKitSnapshot.mm` uses, so a box
// shadow is invisible to this project's own snapshots on macOS, and a window
// capture through CGWindowList needs a permission no CI runner grants. The GTK
// side has the render tree, which is why its half of this feature is checked
// node by node; here the layers and their paths are the observable thing.
namespace {

RnAppKitBoxShadow outsetShadow(CGFloat dx, CGFloat dy, CGFloat blur, CGFloat spread) {
  RnAppKitBoxShadow shadow{};
  shadow.dx = dx;
  shadow.dy = dy;
  shadow.blur = blur;
  shadow.spread = spread;
  shadow.color[3] = 1;
  shadow.inset = false;
  return shadow;
}

// A view inside a parent, which is where a view with an outset shadow has to be
// for the shadow to have anywhere to go.
RnAppKitView *shadowed(const RnAppKitBoxShadow *shadows, NSInteger count, CGRect frame) {
  RnAppKitView *parent = [RnAppKitView viewWithTag:1];
  [parent setRnFrameX:0 y:0 width:200 height:200];
  RnAppKitView *view = [RnAppKitView viewWithTag:2];
  [parent insertRnChild:view atIndex:0];
  [view setRnFrameX:frame.origin.x y:frame.origin.y
              width:frame.size.width height:frame.size.height];
  [view setRnBoxShadows:shadows count:count];
  return view;
}

} // namespace

TEST(box_shadow_becomes_a_layer_carrying_its_own_numbers) {
  @autoreleasepool {
    const RnAppKitBoxShadow shadow = outsetShadow(2, 4, 8, 1);
    RnAppKitView *view = shadowed(&shadow, 1, CGRectMake(50, 50, 100, 60));

    EXPECT_EQ((long)view.rnBoxShadowCount, 1L);
    EXPECT_EQ((long)view.rnBoxShadowLayers.count, 1L);
    CALayer *layer = view.rnBoxShadowLayers.firstObject;

    // Half the blur radius, which is CSS's radius as a gaussian sigma.
    EXPECT_NEAR((int)(layer.shadowRadius * 100), 400, 1);
    EXPECT_NEAR((int)layer.shadowOpacity, 1, 0);
    // The offset is in the path, not in shadowOffset: this view is flipped, and
    // a path in its own coordinates comes out pointing the right way.
    EXPECT_EQ((int)layer.shadowOffset.width, 0);
    EXPECT_EQ((int)layer.shadowOffset.height, 0);

    // The path is the box, grown by the spread and moved by the offset. Checking
    // the bounding box catches an offset on the wrong axis and a spread read as a
    // blur, which both draw a plausible shadow.
    const CGRect box = CGPathGetBoundingBox(layer.shadowPath);
    EXPECT_EQ((int)box.origin.x, 1);    // -spread + dx
    EXPECT_EQ((int)box.origin.y, 3);    // -spread + dy
    EXPECT_EQ((int)box.size.width, 102);
    EXPECT_EQ((int)box.size.height, 62);

    // And it is masked, or the shadow would show through a translucent box.
    EXPECT(layer.mask != nil);
    EXPECT([((CAShapeLayer *)layer.mask).fillRule isEqualToString:kCAFillRuleEvenOdd]);
  }
}

// Where the layer lives. Inside the view normally, which is where it composites
// exactly as the view does.
TEST(box_shadow_an_outset_shadow_sits_inside_the_view_that_casts_it) {
  @autoreleasepool {
    const RnAppKitBoxShadow shadow = outsetShadow(0, 4, 8, 0);
    RnAppKitView *view = shadowed(&shadow, 1, CGRectMake(50, 50, 100, 60));

    CALayer *layer = view.rnBoxShadowLayers.firstObject;
    EXPECT(layer != nil);
    EXPECT(layer.superlayer == view.layer);
  }
}

// And in the parent when the view clips itself, which is the case the obvious
// arrangement loses entirely: masksToBounds clips a sublayer, and CSS does not
// clip an element's own shadow for `overflow: 'hidden'`.
TEST(box_shadow_a_clipping_view_casts_its_shadow_into_the_parent) {
  @autoreleasepool {
    const RnAppKitBoxShadow shadow = outsetShadow(0, 4, 8, 0);
    RnAppKitView *view = shadowed(&shadow, 1, CGRectMake(50, 50, 100, 60));
    [view setRnClipsChildren:YES];

    CALayer *layer = view.rnBoxShadowLayers.firstObject;
    EXPECT(layer != nil);
    EXPECT(layer.superlayer == view.superview.layer);
    // In the parent's coordinates, so the shadow is still where the view is.
    EXPECT_EQ((int)layer.frame.origin.x, 50);
    EXPECT_EQ((int)layer.frame.origin.y, 50);

    // And back inside when the view stops clipping.
    [view setRnClipsChildren:NO];
    EXPECT(view.rnBoxShadowLayers.firstObject.superlayer == view.layer);
    EXPECT(layer.superlayer == nil);
  }
}

// The other way a view clips its own layer: radii too elliptical for CALayer's
// cornerRadius, which get a mask layer. Same consequence, same answer.
TEST(box_shadow_elliptical_radii_also_move_the_shadow_out) {
  @autoreleasepool {
    const RnAppKitBoxShadow shadow = outsetShadow(0, 4, 8, 0);
    RnAppKitView *view = shadowed(&shadow, 1, CGRectMake(50, 50, 100, 60));
    const CGFloat radii[8] = {20, 8, 20, 8, 20, 8, 20, 8};
    [view setRnBorderRadii:radii];

    EXPECT(view.layer.mask != nil);
    EXPECT(view.rnBoxShadowLayers.firstObject.superlayer == view.superview.layer);
    // The shadow follows the radii it is cast by, so its path is rounded too.
    const CGRect box = CGPathGetBoundingBox(view.rnBoxShadowLayers.firstObject.shadowPath);
    EXPECT_EQ((int)box.size.width, 100);
  }
}

// An inset shadow is the other way round: inside the view, where clipping is
// right, and with the hole punched out of its path so the blur falls inward.
TEST(box_shadow_an_inset_shadow_sits_inside_the_view) {
  @autoreleasepool {
    RnAppKitBoxShadow shadow = outsetShadow(0, 2, 4, 0);
    shadow.inset = true;
    RnAppKitView *view = shadowed(&shadow, 1, CGRectMake(50, 50, 100, 60));

    CALayer *layer = view.rnBoxShadowLayers.firstObject;
    EXPECT(layer != nil);
    EXPECT(layer.superlayer == view.layer);
    // The path is bigger than the view, being the ring the shadow is cast from,
    // and the mask keeps it inside.
    const CGRect box = CGPathGetBoundingBox(layer.shadowPath);
    EXPECT(box.size.width > 100);
    EXPECT(layer.mask != nil);
    // Not even-odd: the hole is a reversed subpath, so the winding punches it.
    EXPECT(![((CAShapeLayer *)layer.mask).fillRule isEqualToString:kCAFillRuleEvenOdd]);
  }
}

TEST(box_shadow_every_shadow_in_the_list_becomes_a_layer) {
  @autoreleasepool {
    RnAppKitBoxShadow shadows[3] = {
        outsetShadow(0, 1, 2, 0),
        outsetShadow(0, 8, 24, -4),
        outsetShadow(0, 1, 0, 0),
    };
    shadows[2].inset = true;
    RnAppKitView *view = shadowed(shadows, 3, CGRectMake(50, 50, 100, 60));

    EXPECT_EQ((long)view.rnBoxShadowCount, 3L);
    EXPECT_EQ((long)view.rnBoxShadowLayers.count, 3L);
    // Back to front: the first shadow in the list is the one on top, so the
    // layers come out in the reverse of the list -- the inset one with no blur
    // first, then the wide soft one, then the tight one.
    EXPECT_NEAR((int)view.rnBoxShadowLayers[0].shadowRadius, 0, 0);
    EXPECT_NEAR((int)view.rnBoxShadowLayers[1].shadowRadius, 12, 0);
    EXPECT_NEAR((int)view.rnBoxShadowLayers[2].shadowRadius, 1, 0);
  }
}

TEST(box_shadow_can_be_taken_away_again) {
  @autoreleasepool {
    const RnAppKitBoxShadow shadow = outsetShadow(0, 4, 8, 0);
    RnAppKitView *view = shadowed(&shadow, 1, CGRectMake(50, 50, 100, 60));
    CALayer *layer = view.rnBoxShadowLayers.firstObject;

    [view setRnBoxShadows:nullptr count:0];
    EXPECT_EQ((long)view.rnBoxShadowCount, 0L);
    EXPECT_EQ((long)view.rnBoxShadowLayers.count, 0L);
    // And it left the parent, or the shadow outlives the prop that asked for it.
    EXPECT(layer.superlayer == nil);
  }
}

// A view that is unmounted takes its shadow with it. The shadow lives in the
// parent, so this is the one way this arrangement can leak something visible.
TEST(box_shadow_leaves_the_parent_when_the_view_does) {
  @autoreleasepool {
    const RnAppKitBoxShadow shadow = outsetShadow(0, 4, 8, 0);
    RnAppKitView *view = shadowed(&shadow, 1, CGRectMake(50, 50, 100, 60));
    [view setRnClipsChildren:YES];
    NSView *parent = view.superview;
    CALayer *layer = view.rnBoxShadowLayers.firstObject;
    // In the parent, because the view clips. AppKit has not attached the view's
    // own layer yet -- it does that at the first display -- so the shadow is all
    // there is to find there.
    EXPECT(layer.superlayer == parent.layer);

    [view removeFromSuperview];
    EXPECT(layer.superlayer == nil);
    EXPECT_EQ((long)view.rnBoxShadowLayers.count, 0L);
  }
}

TEST(box_shadow_with_no_colour_is_not_drawn) {
  @autoreleasepool {
    RnAppKitBoxShadow shadow = outsetShadow(0, 4, 8, 0);
    shadow.color[3] = 0;
    RnAppKitView *view = shadowed(&shadow, 1, CGRectMake(50, 50, 100, 60));

    // Still one prop, no layer: what is skipped is the painting, not the prop.
    EXPECT_EQ((long)view.rnBoxShadowCount, 1L);
    EXPECT_EQ((long)view.rnBoxShadowLayers.count, 0L);
  }
}

// And ink lands outside the box, which is the only assertion here that somebody
// looking at the screen would make.
//
// The whole list in the tree dump, which is what makes the wiring testable: the
// prop is read in AppKitMountingManager, and every test above sets the shadows on
// the view by hand. Spelled as GTK spells them, so the cross-host diff compares.
TEST(box_shadows_are_reported_in_the_tree) {
  @autoreleasepool {
    RnAppKitBoxShadow shadows[2] = {outsetShadow(2, 4, 8, 1), outsetShadow(0, 1, 0, 0)};
    shadows[0].color[3] = 0.25;
    shadows[1].color[0] = 1;
    shadows[1].color[1] = 1;
    shadows[1].color[2] = 1;
    shadows[1].inset = true;
    RnAppKitView *view = shadowed(shadows, 2, CGRectMake(50, 50, 100, 60));

    const std::string described = [view describeTree].UTF8String;
    EXPECT(described.find("shadow=(2,4,8,1,#00000040)") != std::string::npos);
    EXPECT(described.find("shadow=(inset 0,1,0,0,#ffffffff)") != std::string::npos);
  }
}

// `backgroundImage` as a linear gradient.
//
// Pixels here, unlike the shadows: a gradient is drawn in `drawRect:` with Core
// Graphics, so the same bitmap the border tests use shows it. What is asserted is
// the ramp itself -- a colour at one end, the other colour at the other, and
// something in between in the middle -- because a gradient drawn along the wrong
// line, or with its stops reversed, is still a gradient.
//
// The arithmetic is not asserted here. The angle and the stop fixup are shared
// with the GTK host in core/Gradients.h and have their own tests; this is the
// drawing.
namespace {

RnAppKitGradientStop gradientStop(CGFloat offset, CGFloat red, CGFloat green, CGFloat blue) {
  RnAppKitGradientStop stop{};
  stop.offset = offset;
  stop.color[0] = red;
  stop.color[1] = green;
  stop.color[2] = blue;
  stop.color[3] = 1;
  return stop;
}

// The red, green and blue of one pixel of the drawn view.
struct Pixel {
  int red;
  int green;
  int blue;
};

Pixel pixelAt(RnAppKitView *view, CGSize size, int x, int y) {
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height,
                                               8, 0, space, kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(context, 1, 1, 1, 1);
  CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
  CGContextTranslateCTM(context, 0, size.height);
  CGContextScaleCTM(context, 1, -1);

  NSGraphicsContext *previous = NSGraphicsContext.currentContext;
  NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context
                                                                             flipped:YES];
  [view drawRect:NSMakeRect(0, 0, size.width, size.height)];
  NSGraphicsContext.currentContext = previous;

  auto *bytes = static_cast<unsigned char *>(CGBitmapContextGetData(context));
  const size_t stride = CGBitmapContextGetBytesPerRow(context);
  const size_t at = (size_t)y * stride + (size_t)x * 4;
  const Pixel pixel{bytes[at], bytes[at + 1], bytes[at + 2]};
  CGContextRelease(context);
  return pixel;
}

RnAppKitView *gradientView(const RnAppKitLinearGradient *gradients, NSInteger count, CGSize size) {
  RnAppKitView *view = [RnAppKitView viewWithTag:1];
  [view setRnFrameX:0 y:0 width:size.width height:size.height];
  [view setRnLinearGradients:gradients count:count];
  return view;
}

} // namespace

TEST(gradient_a_left_to_right_ramp_is_drawn_across_the_box) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 40);
    const RnAppKitGradientStop stops[2] = {gradientStop(0, 1, 0, 0), gradientStop(1, 0, 0, 1)};
    const RnAppKitLinearGradient gradient{CGPointMake(0, 0), CGPointMake(100, 0), stops, 2};
    RnAppKitView *view = gradientView(&gradient, 1, size);

    EXPECT_EQ((long)view.rnLinearGradientCount, 1L);
    const Pixel left = pixelAt(view, size, 2, 20);
    const Pixel middle = pixelAt(view, size, 50, 20);
    const Pixel right = pixelAt(view, size, 97, 20);

    // Red at the left, blue at the right, and mixed in between -- which is the
    // assertion that fails if the line runs the other way.
    EXPECT(left.red > 200 && left.blue < 60);
    EXPECT(right.blue > 200 && right.red < 60);
    EXPECT(middle.red > 60 && middle.red < 200);
    EXPECT(middle.blue > 60 && middle.blue < 200);
  }
}

// The colours extend past the ends of the gradient line, which CSS requires and
// which matters because the line is usually shorter than the box: without it a
// diagonal gradient leaves two corners unpainted.
TEST(gradient_extends_beyond_the_ends_of_its_line) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 40);
    // A line covering only the middle fifth of the box.
    const RnAppKitGradientStop stops[2] = {gradientStop(0, 1, 0, 0), gradientStop(1, 0, 0, 1)};
    const RnAppKitLinearGradient gradient{CGPointMake(40, 0), CGPointMake(60, 0), stops, 2};
    RnAppKitView *view = gradientView(&gradient, 1, size);

    const Pixel left = pixelAt(view, size, 2, 20);
    const Pixel right = pixelAt(view, size, 97, 20);
    // Still the end colours rather than the white the view was drawn onto.
    EXPECT(left.red > 200 && left.blue < 60);
    EXPECT(right.blue > 200 && right.red < 60);
  }
}

// Several gradients, back to front: the first in the list is on top, so a second
// opaque one underneath it must not win.
TEST(gradient_the_first_in_the_list_is_on_top) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 40);
    const RnAppKitGradientStop top[2] = {gradientStop(0, 1, 0, 0), gradientStop(1, 1, 0, 0)};
    const RnAppKitGradientStop bottom[2] = {gradientStop(0, 0, 1, 0), gradientStop(1, 0, 1, 0)};
    const RnAppKitLinearGradient gradients[2] = {
        {CGPointMake(0, 0), CGPointMake(100, 0), top, 2},
        {CGPointMake(0, 0), CGPointMake(100, 0), bottom, 2},
    };
    RnAppKitView *view = gradientView(gradients, 2, size);

    EXPECT_EQ((long)view.rnLinearGradientCount, 2L);
    const Pixel middle = pixelAt(view, size, 50, 20);
    EXPECT(middle.red > 200);
    EXPECT(middle.green < 60);
  }
}

TEST(gradient_can_be_taken_away_again) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 40);
    const RnAppKitGradientStop stops[2] = {gradientStop(0, 1, 0, 0), gradientStop(1, 0, 0, 1)};
    const RnAppKitLinearGradient gradient{CGPointMake(0, 0), CGPointMake(100, 0), stops, 2};
    RnAppKitView *view = gradientView(&gradient, 1, size);

    [view setRnLinearGradients:nullptr count:0];
    EXPECT_EQ((long)view.rnLinearGradientCount, 0L);
    // White, which is what the bitmap was filled with: nothing was drawn.
    const Pixel middle = pixelAt(view, size, 50, 20);
    EXPECT(middle.red > 250 && middle.green > 250 && middle.blue > 250);
  }
}

// Rounded corners clip it, as they clip the background colour.
TEST(gradient_is_clipped_to_the_rounded_box) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 40);
    const RnAppKitGradientStop stops[2] = {gradientStop(0, 1, 0, 0), gradientStop(1, 0, 0, 1)};
    const RnAppKitLinearGradient gradient{CGPointMake(0, 0), CGPointMake(100, 0), stops, 2};
    RnAppKitView *view = gradientView(&gradient, 1, size);
    [view setRnCornerRadius:20];

    // The top-left corner is outside a 20pt radius, so it keeps the white it was
    // drawn onto; the middle of the left edge is inside and is painted.
    const Pixel corner = pixelAt(view, size, 1, 1);
    const Pixel edge = pixelAt(view, size, 2, 20);
    EXPECT(corner.red > 250 && corner.blue > 250);
    EXPECT(edge.red > 200 && edge.blue < 60);
  }
}

// And the gradients are in the tree dump, spelled as GTK spells them, which is
// what makes the wiring testable: the angle and the stops are resolved in
// AppKitMountingManager and the tests above hand the view the answer.
TEST(gradients_are_reported_in_the_tree) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(100, 60);
    const RnAppKitGradientStop stops[3] = {
        gradientStop(0, 1, 0, 0), gradientStop(0.5, 0, 1, 0), gradientStop(1, 0, 0, 1)};
    const RnAppKitLinearGradient gradient{CGPointMake(0, 60), CGPointMake(0, 0), stops, 3};
    RnAppKitView *view = gradientView(&gradient, 1, size);

    const std::string described = [view describeTree].UTF8String;
    EXPECT(described.find("gradient=((0,60)-(0,0),3 stops)") != std::string::npos);
  }
}

// `outlineWidth`, `outlineColor`, `outlineOffset` and `outlineStyle`.
//
// CSS's outline is not a border: it is drawn outside the box, it takes no layout
// space, and it is one ring rather than four edges. So the things worth asserting
// are the geometry of the ring -- which is the box grown by the offset plus half
// the stroke width, since a stroke straddles its path -- and where the layer that
// carries it lives, for the same reason the box shadows care: a view that clips
// itself must not clip its own outline away.
//
// No pixels, for the reason given above the box shadow tests: a CAShapeLayer's
// stroke is drawn by Core Animation, and `renderInContext:` does not draw it.
namespace {

RnAppKitView *outlined(CGFloat width, CGFloat offset, RnAppKitBorderStyle style, CGRect frame) {
  RnAppKitView *parent = [RnAppKitView viewWithTag:1];
  [parent setRnFrameX:0 y:0 width:200 height:200];
  RnAppKitView *view = [RnAppKitView viewWithTag:2];
  [parent insertRnChild:view atIndex:0];
  [view setRnFrameX:frame.origin.x y:frame.origin.y
              width:frame.size.width height:frame.size.height];
  const CGFloat black[4] = {0, 0, 0, 1};
  [view setRnOutlineWidth:width offset:offset color:black style:style];
  return view;
}

} // namespace

TEST(outline_becomes_a_stroked_layer_outside_the_box) {
  @autoreleasepool {
    RnAppKitView *view = outlined(4, 0, RnAppKitBorderStyleSolid, CGRectMake(50, 50, 100, 60));
    CAShapeLayer *layer = view.rnOutlineLayer;
    EXPECT(layer != nil);

    // Stroked, not filled: a filled shape would cover the view.
    EXPECT(layer.fillColor == nil);
    EXPECT(layer.strokeColor != nil);
    EXPECT_EQ((int)layer.lineWidth, 4);

    // Half a width outside the box, so the inner edge of the ring touches it.
    const CGRect box = CGPathGetBoundingBox(layer.path);
    EXPECT_EQ((int)box.origin.x, -2);
    EXPECT_EQ((int)box.origin.y, -2);
    EXPECT_EQ((int)box.size.width, 104);
    EXPECT_EQ((int)box.size.height, 64);

    // Solid means no dashes, which is what distinguishes it from the two below.
    EXPECT(layer.lineDashPattern == nil);
  }
}

// The offset, which is the whole point of an outline over a border: it leaves a
// gap. A width read as an offset, or an offset ignored, both still draw a ring,
// so the bounding box is what catches it.
TEST(outline_an_offset_pushes_the_ring_further_out) {
  @autoreleasepool {
    RnAppKitView *view = outlined(4, 6, RnAppKitBorderStyleSolid, CGRectMake(50, 50, 100, 60));
    const CGRect box = CGPathGetBoundingBox(view.rnOutlineLayer.path);
    EXPECT_EQ((int)box.origin.x, -8);  // -(offset + width / 2)
    EXPECT_EQ((int)box.origin.y, -8);
    EXPECT_EQ((int)box.size.width, 116);
    EXPECT_EQ((int)box.size.height, 76);
  }
}

TEST(outline_dotted_and_dashed_become_dash_patterns) {
  @autoreleasepool {
    RnAppKitView *dotted = outlined(4, 0, RnAppKitBorderStyleDotted, CGRectMake(0, 0, 100, 60));
    EXPECT_EQ((long)dotted.rnOutlineLayer.lineDashPattern.count, 2L);
    // A zero-length segment with a round cap is a dot, and the gap is what makes
    // it read as dotted rather than as a solid line of dots.
    EXPECT_EQ(dotted.rnOutlineLayer.lineDashPattern.firstObject.intValue, 0);
    EXPECT([dotted.rnOutlineLayer.lineCap isEqualToString:kCALineCapRound]);

    RnAppKitView *dashed = outlined(4, 0, RnAppKitBorderStyleDashed, CGRectMake(0, 0, 100, 60));
    EXPECT_EQ((long)dashed.rnOutlineLayer.lineDashPattern.count, 2L);
    EXPECT(dashed.rnOutlineLayer.lineDashPattern.firstObject.intValue > 0);
  }
}

// No width is no outline, and neither is a transparent colour. The default props
// carry a zero width, so every view in every app goes through this path.
TEST(outline_nothing_is_drawn_without_a_width_or_a_colour) {
  @autoreleasepool {
    RnAppKitView *none = outlined(0, 0, RnAppKitBorderStyleSolid, CGRectMake(0, 0, 100, 60));
    EXPECT(none.rnOutlineLayer == nil);

    RnAppKitView *view = outlined(4, 0, RnAppKitBorderStyleSolid, CGRectMake(0, 0, 100, 60));
    EXPECT(view.rnOutlineLayer != nil);
    const CGFloat clear[4] = {0, 0, 0, 0};
    [view setRnOutlineWidth:4 offset:0 color:clear style:RnAppKitBorderStyleSolid];
    EXPECT(view.rnOutlineLayer == nil);
  }
}

// Where the layer lives, which decides whether `overflow: 'hidden'` eats the
// outline. Same rule as the box shadows, and the same failure if it is wrong.
TEST(outline_a_clipping_view_puts_its_outline_in_the_parent) {
  @autoreleasepool {
    RnAppKitView *view = outlined(4, 0, RnAppKitBorderStyleSolid, CGRectMake(50, 50, 100, 60));
    EXPECT(view.rnOutlineLayer.superlayer == view.layer);

    [view setRnClipsChildren:YES];
    CAShapeLayer *layer = view.rnOutlineLayer;
    EXPECT(layer.superlayer == view.superview.layer);
    // In the parent's coordinates, so the ring is still around the view.
    EXPECT_EQ((int)layer.frame.origin.x, 50);
    EXPECT_EQ((int)layer.frame.origin.y, 50);

    [view setRnClipsChildren:NO];
    EXPECT(view.rnOutlineLayer.superlayer == view.layer);
  }
}

// The ring is concentric with the box, so a rounded box gets a rounded ring with
// the radius grown by the same amount the ring moved out. A square ring around a
// rounded box is the wrong answer that looks almost right, so the corner itself
// is what gets asserted: inside a square path, outside a rounded one.
TEST(outline_follows_the_corner_radius) {
  @autoreleasepool {
    RnAppKitView *square = outlined(4, 0, RnAppKitBorderStyleSolid, CGRectMake(50, 50, 100, 60));
    const CGRect box = CGPathGetBoundingBox(square.rnOutlineLayer.path);
    const CGPoint corner = CGPointMake(box.origin.x + 1, box.origin.y + 1);
    EXPECT(CGPathContainsPoint(square.rnOutlineLayer.path, NULL, corner, false));

    RnAppKitView *rounded = outlined(4, 0, RnAppKitBorderStyleSolid, CGRectMake(50, 50, 100, 60));
    [rounded setRnCornerRadius:20];
    EXPECT(!CGPathContainsPoint(rounded.rnOutlineLayer.path, NULL, corner, false));
    // And still only as big as the square one, because the radius grew rather
    // than the ring.
    const CGRect roundedBox = CGPathGetBoundingBox(rounded.rnOutlineLayer.path);
    EXPECT_EQ((int)roundedBox.size.width, (int)box.size.width);
  }
}

// And in the tree dump, spelled as GTK spells it, which is what lets one e2e
// scenario check the wiring on both hosts.
TEST(outline_is_reported_in_the_tree) {
  @autoreleasepool {
    RnAppKitView *view = outlined(3, 2, RnAppKitBorderStyleDashed, CGRectMake(0, 0, 100, 60));
    const std::string described = [view describeTree].UTF8String;
    EXPECT(described.find("outline=(3,2,#000000ff,dashed)") != std::string::npos);
  }
}
