// The overlay scrollbar, on the macOS view layer.
//
// core/ScrollIndicator.h is where the geometry is decided, and
// tests/test_scroll_indicator.cpp already checks the arithmetic. What is left
// is the part that is only true on this platform: AppKit draws subviews *over*
// their superview, and a ScrollView always has one -- its content -- covering
// the whole of it. So the thumb is a view of its own, kept last among the
// subviews, which is AppKit's definition of topmost.
//
// That is the decision worth a test. A thumb drawn in the scroll view's own
// `drawRect:` would compute exactly the same numbers, report exactly the same
// dump line, and be invisible behind the list.

#include "ScrollIndicator.h"
#include "TestHarness.h"

#import "RnAppKitView.h"

namespace {

// The view the scrollbars are drawn into, which is deliberately not an
// RnAppKitView -- that is what keeps it out of the paint order, out of hit
// testing and out of the tree dump's children.
NSView *indicatorViewOf(RnAppKitView *view) {
  NSView *last = view.subviews.lastObject;
  if (last == nil || [last isKindOfClass:[RnAppKitView class]]) {
    return nil;
  }
  return last;
}

RnAppKitView *scrollViewWithIndicators(CGFloat width, CGFloat height) {
  RnAppKitView *view = [RnAppKitView viewWithTag:1];
  [view setRnFrameX:0 y:0 width:width height:height];
  // A hundred-point window onto four hundred points of content, which
  // core/ScrollIndicator.h turns into a quarter-length thumb.
  const basalt::ScrollIndicator vertical = basalt::scrollIndicatorFor(height, height * 4, 0);
  [view setRnScrollIndicatorVerticalOffset:vertical.offset
                            verticalLength:vertical.length
                          horizontalOffset:0
                          horizontalLength:0];
  return view;
}

// Draws one view into a white bitmap and answers whether anything landed on the
// pixel asked for. Alpha 0.35 black over white is mid-grey, so "darker than
// white" is the whole test -- nothing here pins a colour.
bool inkAt(NSView *view, CGSize size, int atX, int atY) {
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height, 8,
                                               0, space, kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(context, 1, 1, 1, 1);
  CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
  // A CGBitmapContext counts y from the bottom and this view counts it from the
  // top, and `graphicsContextWithCGContext:flipped:` only *declares* which way
  // up a context is -- it applies no transform. Without this the assertions
  // below would be upside down, and a thumb at the bottom would pass a test
  // written for one at the top.
  CGContextTranslateCTM(context, 0, size.height);
  CGContextScaleCTM(context, 1, -1);

  NSGraphicsContext *previous = NSGraphicsContext.currentContext;
  NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithCGContext:context
                                                                             flipped:YES];
  [view drawRect:NSMakeRect(0, 0, size.width, size.height)];
  NSGraphicsContext.currentContext = previous;

  auto *pixels = static_cast<unsigned char *>(CGBitmapContextGetData(context));
  const size_t stride = CGBitmapContextGetBytesPerRow(context);
  const bool marked = pixels[(size_t)atY * stride + (size_t)atX * 4] < 250;
  CGContextRelease(context);
  return marked;
}

} // namespace

// The thumb exists, and it is the last subview -- which is the one AppKit draws
// on top of the others.
TEST(appkit_scrollbar_is_the_topmost_subview) {
  RnAppKitView *view = scrollViewWithIndicators(100, 100);
  NSView *indicators = indicatorViewOf(view);
  EXPECT(indicators != nil);

  // A content view arrives, as one always does: React Native wraps a
  // ScrollView's children in exactly one.
  RnAppKitView *content = [RnAppKitView viewWithTag:2];
  [content setRnFrameX:0 y:0 width:100 height:400];
  [view insertRnChild:content atIndex:0];
  EXPECT(indicatorViewOf(view) == indicators);

  // And again past the end of the list, which is the insert that appends.
  RnAppKitView *second = [RnAppKitView viewWithTag:3];
  [view insertRnChild:second atIndex:5];
  EXPECT(indicatorViewOf(view) == indicators);
}

// It is paint and nothing else: a press goes to the content underneath.
TEST(appkit_scrollbar_is_not_a_hit_target) {
  RnAppKitView *view = scrollViewWithIndicators(100, 100);
  NSView *indicators = indicatorViewOf(view);
  EXPECT([indicators hitTest:NSMakePoint(97, 10)] == nil);
}

// The thumb lands at the trailing edge, at the top, and nowhere else.
TEST(appkit_scrollbar_draws_at_the_trailing_edge) {
  RnAppKitView *view = scrollViewWithIndicators(100, 100);
  NSView *indicators = indicatorViewOf(view);
  EXPECT(indicators != nil);

  const CGSize size = CGSizeMake(100, 100);
  // Inside the bar: two in from the right edge, three more into its six points
  // of thickness, and ten down -- which is inside a thumb a quarter of 96 long.
  EXPECT(inkAt(indicators, size, 95, 10));
  // The middle of the view, where a scrollbar has no business being.
  EXPECT(!inkAt(indicators, size, 50, 50));
  // Past the end of the thumb: it starts at the top, so the bottom is bare.
  EXPECT(!inkAt(indicators, size, 95, 90));
}

// Content that fits takes the whole thing away again, rather than leaving a
// zero-length bar behind.
TEST(appkit_scrollbar_goes_away_when_everything_fits) {
  RnAppKitView *view = scrollViewWithIndicators(100, 100);
  EXPECT(indicatorViewOf(view) != nil);

  [view setRnScrollIndicatorVerticalOffset:0
                            verticalLength:0
                          horizontalOffset:0
                          horizontalLength:0];
  EXPECT(indicatorViewOf(view) == nil);
}
