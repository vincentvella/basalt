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
