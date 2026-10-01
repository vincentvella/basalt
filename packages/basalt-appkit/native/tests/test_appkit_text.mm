// Tests for the Core Text layer: does a paragraph measure and truncate the way
// React Native means it to?
//
// Against RnTextLayout directly, with no Fabric and no React Native, for the
// same reason the view tests are: this is the half that can be wrong on its
// own, and a failure here is much easier to read than the same failure arriving
// as a paragraph that is the wrong height on screen.
//
// Nothing here asserts an exact pixel size. Core Text over the system font is
// not reproducible across macOS versions, and a test that pinned it would fail
// on somebody else's machine for no useful reason. What is asserted is the
// relationships that must hold whatever the font is.

#include "TestHarness.h"

#import "RnTextLayout.h"

#include <sstream>

namespace {

NSAttributedString *styled(NSString *text, CGFloat size, NSTextAlignment alignment) {
  NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];
  style.alignment = alignment;
  style.lineBreakMode = NSLineBreakByWordWrapping;
  return [[NSAttributedString alloc] initWithString:text
                                         attributes:@{
                                           NSFontAttributeName : [NSFont systemFontOfSize:size],
                                           NSParagraphStyleAttributeName : style,
                                         }];
}

RnTextLayout *layoutFor(NSString *text, CGFloat size, NSInteger lines) {
  return [RnTextLayout layoutWithAttributedString:styled(text, size, NSTextAlignmentNatural)
                             maximumNumberOfLines:lines
                                   truncationType:kCTLineTruncationEnd
                                        truncates:YES];
}

NSString *const kLong =
    @"A paragraph long enough to wrap several times over, so that the line breaking has real "
    @"work to do and a line limit has something to cut.";

} // namespace

TEST(text_measures_something) {
  @autoreleasepool {
    RnTextLayout *layout = layoutFor(@"Hello", 16, 0);
    const CGSize size = [layout sizeForWidth:-1];

    // The stub this replaces returned the minimum size, which is how text had
    // no size at all on this platform until now.
    EXPECT(size.width > 0);
    EXPECT(size.height > 0);
  }
}

TEST(text_at_a_bigger_size_measures_bigger) {
  @autoreleasepool {
    const CGSize small = [layoutFor(@"Hello", 12, 0) sizeForWidth:-1];
    const CGSize large = [layoutFor(@"Hello", 32, 0) sizeForWidth:-1];

    EXPECT(large.width > small.width);
    EXPECT(large.height > small.height);
  }
}

TEST(text_wraps_when_it_is_given_a_width) {
  @autoreleasepool {
    RnTextLayout *layout = layoutFor(kLong, 16, 0);

    const CGSize unconstrained = [layout sizeForWidth:-1];
    const CGSize narrow = [layout sizeForWidth:200];

    // Wrapping trades width for height. Both halves matter: a layout that
    // ignored the width would keep its height, and one that ignored the text
    // would keep its width.
    EXPECT(narrow.width <= 200);
    EXPECT(narrow.height > unconstrained.height);
  }
}

TEST(number_of_lines_limits_the_height) {
  @autoreleasepool {
    const CGSize unlimited = [layoutFor(kLong, 16, 0) sizeForWidth:200];
    const CGSize twoLines = [layoutFor(kLong, 16, 2) sizeForWidth:200];
    const CGSize oneLine = [layoutFor(kLong, 16, 1) sizeForWidth:200];

    EXPECT(twoLines.height < unlimited.height);
    EXPECT(oneLine.height < twoLines.height);

    // Two lines is two lines tall, not "whatever fits". Core Text has no line
    // limit of its own -- RnTextLayout applies it by keeping the first N lines
    // -- so this is the assertion that the limit is really being applied rather
    // than the text happening to be short.
    EXPECT_NEAR(twoLines.height, oneLine.height * 2, 2.0);
  }
}

// A limit that drops nothing must not add an ellipsis. Getting this wrong is
// invisible until a paragraph that exactly fits gets one anyway.
TEST(a_line_limit_that_fits_is_not_truncated) {
  @autoreleasepool {
    const CGSize unlimited = [layoutFor(@"Short", 16, 0) sizeForWidth:400];
    const CGSize limited = [layoutFor(@"Short", 16, 3) sizeForWidth:400];

    EXPECT_NEAR(limited.width, unlimited.width, 0.51);
    EXPECT_NEAR(limited.height, unlimited.height, 0.51);
  }
}

// Alignment changes where a line is drawn, never how big it is. A paragraph
// that measured differently when centred would make Yoga lay it out wrong.
TEST(alignment_does_not_change_the_measured_size) {
  @autoreleasepool {
    const auto measure = [](NSTextAlignment alignment) {
      RnTextLayout *layout =
          [RnTextLayout layoutWithAttributedString:styled(kLong, 16, alignment)
                              maximumNumberOfLines:0
                                    truncationType:kCTLineTruncationEnd
                                         truncates:YES];
      return [layout sizeForWidth:300];
    };

    const CGSize natural = measure(NSTextAlignmentNatural);
    const CGSize centred = measure(NSTextAlignmentCenter);
    const CGSize right = measure(NSTextAlignmentRight);

    EXPECT_NEAR(centred.height, natural.height, 0.51);
    EXPECT_NEAR(right.height, natural.height, 0.51);
  }
}

// The drawing path, against a real bitmap: a laid-out paragraph has to put ink
// on the page, and it has to put it in the half of the box the alignment says.
TEST(text_draws_where_the_alignment_says) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(300, 40);

    const auto inkColumns = [&](NSTextAlignment alignment) {
      RnTextLayout *layout =
          [RnTextLayout layoutWithAttributedString:styled(@"Edge", 16, alignment)
                              maximumNumberOfLines:0
                                    truncationType:kCTLineTruncationEnd
                                         truncates:YES];

      CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height,
                                                   8, 0, space, kCGImageAlphaPremultipliedLast);
      CGColorSpaceRelease(space);
      CGContextSetRGBFillColor(context, 1, 1, 1, 1);
      CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
      [layout drawInContext:context size:size];

      auto *pixels = static_cast<unsigned char *>(CGBitmapContextGetData(context));
      const size_t stride = CGBitmapContextGetBytesPerRow(context);
      int leftmost = -1;
      for (size_t x = 0; x < (size_t)size.width && leftmost < 0; x++) {
        for (size_t y = 0; y < (size_t)size.height; y++) {
          if (pixels[y * stride + x * 4] < 200) {  // any ink at all
            leftmost = (int)x;
            break;
          }
        }
      }
      CGContextRelease(context);
      return leftmost;
    };

    const int left = inkColumns(NSTextAlignmentNatural);
    const int centred = inkColumns(NSTextAlignmentCenter);
    const int right = inkColumns(NSTextAlignmentRight);

    // Ink at all, first: a paragraph that drew nothing would pass every size
    // assertion above.
    EXPECT(left >= 0);
    EXPECT(centred > left);
    EXPECT(right > centred);
  }
}
