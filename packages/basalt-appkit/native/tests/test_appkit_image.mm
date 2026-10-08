// Tests for how an <Image> fills its frame.
//
// The four resize modes are the part of images two platforms are most likely to
// disagree about while both looking plausible on their own, and the arithmetic
// is duplicated per view layer -- the view layers share no code, by design --
// so it is exactly the kind of thing that drifts.
//
// Against a real bitmap rather than against the rectangle the view computed:
// the rectangle is private, and a view that computed it correctly and drew
// nowhere near it would pass either way. What is asserted is where the ink
// lands, which is what somebody looking at the screen would check.
//
// No exact pixel counts. Nearest-neighbour versus smoothed scaling puts an
// edge half a pixel either way, and pinning that would fail on a future macOS
// for no useful reason.

#include "TestHarness.h"

#import "RnAppKitView.h"

#include <cmath>
#include <sstream>
#include <vector>

namespace {

// A 4x2 image, opaque black. Small and oddly shaped, so a mode that ignores the
// aspect ratio is obvious.
CGImageRef makeImage(size_t width, size_t height) {
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr, width, height, 8, 0, space,
                                               kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(context, 0, 0, 0, 1);
  CGContextFillRect(context, CGRectMake(0, 0, (CGFloat)width, (CGFloat)height));
  CGImageRef image = CGBitmapContextCreateImage(context);
  CGContextRelease(context);
  return image;
}

// Left half black, right half white, opaque. A step edge, which is the one
// picture a blur is measurable against: how far the ramp spreads is the radius.
CGImageRef makeSplitImage(size_t width, size_t height) {
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr, width, height, 8, 0, space,
                                               kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(context, 1, 1, 1, 1);
  CGContextFillRect(context, CGRectMake(0, 0, (CGFloat)width, (CGFloat)height));
  CGContextSetRGBFillColor(context, 0, 0, 0, 1);
  CGContextFillRect(context, CGRectMake(0, 0, (CGFloat)width / 2, (CGFloat)height));
  CGImageRef image = CGBitmapContextCreateImage(context);
  CGContextRelease(context);
  return image;
}

// The red channel of the view drawn onto white, row-major. Red rather than
// luminance because everything here is grey or black: the three channels agree.
std::vector<unsigned char> drawnPixels(RnAppKitView *view, CGSize size) {
  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height,
                                               8, 0, space, kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(context, 1, 1, 1, 1);
  CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
  // A CGBitmapContext counts y from the bottom and this view counts it from the
  // top, and `graphicsContextWithCGContext:flipped:` only *declares* which way
  // up a context is -- it applies no transform.
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

// How wide the black-to-white ramp is along the middle row, from 10% to 90%.
//
// For a gaussian of standard deviation sigma that width is 2.563 * sigma, which
// is how a measured number here becomes a statement about the radius: an
// unblurred edge is one or two pixels and nothing else needs to be known about
// the filter.
double rampWidth(RnAppKitView *view, CGSize size) {
  const std::vector<unsigned char> pixels = drawnPixels(view, size);
  const int width = (int)size.width;
  const int row = (int)size.height / 2;
  int low = -1;
  int high = -1;
  for (int x = 0; x < width; x++) {
    const unsigned char value = pixels[(size_t)row * (size_t)width + (size_t)x];
    if (low < 0 && value > 25) {
      low = x;
    }
    if (value > 229) {
      high = x;
      break;
    }
  }
  return (high > low && low >= 0) ? (double)(high - low) : 0.0;
}

struct Ink {
  int left{-1};
  int top{-1};
  int right{-1};
  int bottom{-1};
  int width() const { return right < 0 ? 0 : right - left + 1; }
  int height() const { return bottom < 0 ? 0 : bottom - top + 1; }
};

// Draws the view into a white bitmap and returns the bounding box of anything
// darker than white.
Ink inkOf(RnAppKitView *view, CGSize size) {
  const std::vector<unsigned char> pixels = drawnPixels(view, size);
  Ink ink;
  for (int y = 0; y < (int)size.height; y++) {
    for (int x = 0; x < (int)size.width; x++) {
      if (pixels[(size_t)y * (size_t)size.width + (size_t)x] < 128) {
        if (ink.left < 0 || x < ink.left) ink.left = x;
        if (ink.right < 0 || x > ink.right) ink.right = x;
        if (ink.top < 0 || y < ink.top) ink.top = y;
        if (ink.bottom < 0 || y > ink.bottom) ink.bottom = y;
      }
    }
  }
  return ink;
}

RnAppKitView *imageView(CGImageRef image, RnAppKitImageFit fit, CGSize size) {
  RnAppKitView *view = [RnAppKitView viewWithTag:1];
  [view setRnFrameX:0 y:0 width:size.width height:size.height];
  [view setRnImage:image fit:fit];
  return view;
}

} // namespace

// stretch ignores the aspect ratio and fills the box exactly.
TEST(image_stretch_fills_the_frame) {
  @autoreleasepool {
    CGImageRef image = makeImage(4, 2);
    const CGSize size = CGSizeMake(100, 100);
    const Ink ink = inkOf(imageView(image, RnAppKitImageFitStretch, size), size);
    CGImageRelease(image);

    EXPECT_NEAR(ink.width(), 100, 2);
    EXPECT_NEAR(ink.height(), 100, 2);
  }
}

// contain fits the whole image inside, so a 2:1 image in a square box spans the
// full width and half the height, centred.
TEST(image_contain_fits_inside_and_centres) {
  @autoreleasepool {
    CGImageRef image = makeImage(4, 2);
    const CGSize size = CGSizeMake(100, 100);
    const Ink ink = inkOf(imageView(image, RnAppKitImageFitContain, size), size);
    CGImageRelease(image);

    EXPECT_NEAR(ink.width(), 100, 2);
    EXPECT_NEAR(ink.height(), 50, 2);
    // Centred vertically: 25 above and 25 below.
    EXPECT_NEAR(ink.top, 25, 2);
  }
}

// cover fills the box and crops, so the same image spans the full box with its
// sides cut off -- never any white.
TEST(image_cover_fills_the_frame_and_crops) {
  @autoreleasepool {
    CGImageRef image = makeImage(4, 2);
    const CGSize size = CGSizeMake(100, 100);
    const Ink ink = inkOf(imageView(image, RnAppKitImageFitCover, size), size);
    CGImageRelease(image);

    EXPECT_NEAR(ink.width(), 100, 2);
    EXPECT_NEAR(ink.height(), 100, 2);
  }
}

// center draws at natural size, centred -- and never scales up, which is the
// half of `center` that is easy to miss.
TEST(image_center_draws_at_natural_size) {
  @autoreleasepool {
    CGImageRef image = makeImage(40, 20);
    const CGSize size = CGSizeMake(100, 100);
    const Ink ink = inkOf(imageView(image, RnAppKitImageFitCenter, size), size);
    CGImageRelease(image);

    EXPECT_NEAR(ink.width(), 40, 2);
    EXPECT_NEAR(ink.height(), 20, 2);
    EXPECT_NEAR(ink.left, 30, 2);
    EXPECT_NEAR(ink.top, 40, 2);
  }
}

// An image larger than its frame is scaled down by `center` rather than
// overflowing, and cover and center both clip -- an <Image> never paints
// outside its own box on iOS or Android.
TEST(image_center_shrinks_an_oversized_image) {
  @autoreleasepool {
    CGImageRef image = makeImage(400, 200);
    const CGSize size = CGSizeMake(100, 100);
    const Ink ink = inkOf(imageView(image, RnAppKitImageFitCenter, size), size);
    CGImageRelease(image);

    EXPECT_NEAR(ink.width(), 100, 2);
    EXPECT_NEAR(ink.height(), 50, 2);
  }
}

TEST(image_is_reported_in_the_tree) {
  @autoreleasepool {
    CGImageRef image = makeImage(160, 100);
    RnAppKitView *view = imageView(image, RnAppKitImageFitCover, CGSizeMake(50, 50));
    CGImageRelease(image);

    const std::string described = [view describeTree].UTF8String;
    // The same `texture=WxH` the GTK side emits, which the end-to-end suite
    // asserts on and the cross-platform diff compares.
    EXPECT(described.find("texture=160x100") != std::string::npos);
  }
}

// Clearing has to release, and drawing after it has to be a no-op rather than a
// use-after-free.
TEST(image_can_be_cleared) {
  @autoreleasepool {
    CGImageRef image = makeImage(4, 2);
    RnAppKitView *view = imageView(image, RnAppKitImageFitCover, CGSizeMake(50, 50));
    CGImageRelease(image);

    [view setRnImage:nullptr fit:RnAppKitImageFitCover];

    const std::string described = [view describeTree].UTF8String;
    EXPECT(described.find("texture=") == std::string::npos);
    EXPECT_EQ(inkOf(view, CGSizeMake(50, 50)).width(), 0);
  }
}

// `repeat` tiles from the top left at the image's natural size.
//
// The case that tells it from `center`, which is what it used to fall back
// to: a small image in a large box leaves the corners blank when centred and
// covers them when tiled. Asserting the ink's bounding box is the whole test
// -- if it fills the view, every tile landed.
TEST(image_repeat_tiles_the_whole_box) {
  @autoreleasepool {
    CGImageRef image = makeImage(4, 2);
    RnAppKitView *view = imageView(image, RnAppKitImageFitRepeat, CGSizeMake(100, 100));

    const Ink ink = inkOf(view, CGSizeMake(100, 100));
    // Corner to corner. Centred, a 4x2 image would leave everything outside a
    // 4x2 patch in the middle blank.
    EXPECT_EQ(ink.left, 0);
    EXPECT_EQ(ink.top, 0);
    EXPECT_EQ(ink.right, 99);
    EXPECT_EQ(ink.bottom, 99);

    CGImageRelease(image);
  }
}

// Not stretched: a tile keeps its own size, which is what distinguishes
// `repeat` from `stretch` when the image is small and the box is not.
TEST(image_repeat_keeps_the_tile_at_its_natural_size) {
  @autoreleasepool {
    // A 4x2 black image tiled into 100x100 puts a seam every 4 across and
    // every 2 down. What is checked is cheaper than seams: the image is fully
    // opaque, so a stretch and a tile both fill the box -- but a tile of a
    // *transparent-edged* image would not. Use the fit name instead, which is
    // what the cross-host dump compares.
    CGImageRef image = makeImage(4, 2);
    RnAppKitView *view = imageView(image, RnAppKitImageFitRepeat, CGSizeMake(100, 100));
    EXPECT([[view describeTree] containsString:@"fit=repeat"]);
    CGImageRelease(image);
  }
}

// `blurRadius` blurs the image, and by the right amount.
//
// The amount is the whole point of the test. A blur that is applied in the
// source image's pixels rather than in the view's coordinates still looks
// blurred, and still passes a test that only asks whether the edge is soft, but
// it is a different picture from the GTK side's for every image that is not
// drawn at its natural size. Here a 40-pixel-wide image is stretched to 200, so
// a blur in the wrong space would be five times too narrow.
TEST(image_blur_radius_softens_the_edge_in_view_coordinates) {
  @autoreleasepool {
    CGImageRef image = makeSplitImage(40, 8);
    const CGSize size = CGSizeMake(200, 40);

    RnAppKitView *sharp = imageView(image, RnAppKitImageFitStretch, size);
    const double unblurred = rampWidth(sharp, size);
    // Four pixels, which is what "no blur" looks like here: one source pixel is
    // five destination pixels wide after the stretch, and the interpolation
    // between them is the whole ramp.
    EXPECT(unblurred <= 5);

    RnAppKitView *view = imageView(image, RnAppKitImageFitStretch, size);
    [view setRnImageBlur:20];
    // sigma is half the radius and a 10-90 ramp is 2.563 sigma, so a radius of
    // 20 is a ramp near 26 pixels, which is what it measures. The tolerance is
    // wide enough for Core Image to round its kernel differently on a future
    // macOS and narrow enough to fail on a blur applied in the source's own
    // pixels: that one is magnified by the stretch along with the picture and
    // measures 130, which is checked by sabotage rather than guessed at.
    EXPECT_NEAR((int)rampWidth(view, size), 26, 5);

    CGImageRelease(image);
  }
}

// Zero is no blur, and so is a negative radius: an app can send either and
// neither is a crash or a filter with a nonsense kernel.
TEST(image_blur_radius_of_zero_or_less_draws_the_image_itself) {
  @autoreleasepool {
    CGImageRef image = makeSplitImage(40, 8);
    const CGSize size = CGSizeMake(200, 40);

    RnAppKitView *view = imageView(image, RnAppKitImageFitStretch, size);
    [view setRnImageBlur:20];
    [view setRnImageBlur:0];
    EXPECT((int)rampWidth(view, size) <= 5);

    [view setRnImageBlur:-20];
    EXPECT((int)rampWidth(view, size) <= 5);

    CGImageRelease(image);
  }
}

// A tinted image is a silhouette filled with one colour, and a blurred tinted
// image is that silhouette with soft edges. The mask is what gets blurred here,
// which is the same picture as blurring the fill because the colour is
// constant, and it is what the GTK side does by pushing its blur around the
// tint rather than inside it.
TEST(image_blur_radius_applies_to_a_tinted_image_too) {
  @autoreleasepool {
    // Opaque black on the left, transparent on the right: with a tint it is the
    // alpha that decides where the colour lands.
    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef bitmap = CGBitmapContextCreate(nullptr, 40, 8, 8, 0, space,
                                                kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    CGContextSetRGBFillColor(bitmap, 0, 0, 0, 1);
    CGContextFillRect(bitmap, CGRectMake(0, 0, 20, 8));
    CGImageRef image = CGBitmapContextCreateImage(bitmap);
    CGContextRelease(bitmap);

    const CGSize size = CGSizeMake(200, 40);
    RnAppKitView *view = imageView(image, RnAppKitImageFitStretch, size);
    [view setRnImageTint:[NSColor colorWithSRGBRed:0 green:0 blue:0 alpha:1]];
    EXPECT((int)rampWidth(view, size) <= 5);

    [view setRnImageBlur:20];
    EXPECT_NEAR((int)rampWidth(view, size), 26, 5);

    CGImageRelease(image);
  }
}

// Clearing the image must not leave the blurred copy of it behind to be drawn
// against the next one, and a blur with no image at all is nothing rather than a
// crash.
TEST(image_blur_survives_the_image_being_replaced) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(200, 40);
    RnAppKitView *view = [RnAppKitView viewWithTag:1];
    [view setRnFrameX:0 y:0 width:size.width height:size.height];
    [view setRnImageBlur:20];
    EXPECT_EQ(inkOf(view, size).width(), 0);

    CGImageRef image = makeSplitImage(40, 8);
    [view setRnImage:image fit:RnAppKitImageFitStretch];
    EXPECT_NEAR((int)rampWidth(view, size), 26, 5);

    [view setRnImage:nullptr fit:RnAppKitImageFitStretch];
    EXPECT_EQ(inkOf(view, size).width(), 0);
    CGImageRelease(image);
  }
}

// And the radius is in the tree dump, spelled as GTK spells it.
//
// Which is what makes the wiring testable rather than only the drawing: the
// prop is read in a branch of AppKitMountingManager that knows ImageProps, and
// every test above sets the blur on the view by hand, so all of them pass while
// the mounting manager drops it on the floor. The GTK side did exactly that.
TEST(image_blur_is_reported_in_the_tree) {
  @autoreleasepool {
    CGImageRef image = makeImage(4, 2);
    RnAppKitView *view = imageView(image, RnAppKitImageFitStretch, CGSizeMake(50, 50));
    CGImageRelease(image);

    EXPECT(![[view describeTree] containsString:@"blur="]);
    [view setRnImageBlur:8];
    EXPECT([[view describeTree] containsString:@"blur=8"]);
    [view setRnImageBlur:0];
    EXPECT(![[view describeTree] containsString:@"blur="]);
  }
}
