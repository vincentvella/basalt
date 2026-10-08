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
