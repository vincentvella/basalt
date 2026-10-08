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

// A sentence with an inline view in the middle, built the way CoreTextLayout.mm
// builds one: the U+FFFC character carrying a run delegate that reports the box.
// Constructed here rather than going through buildTextLayout so the test does not
// need React Native's AttributedString, matching the rest of this file.
CGFloat attachAscent(void *context) {
  return static_cast<CGFloat *>(context)[1];
}
CGFloat attachDescent(void * /*context*/) {
  return 0;
}
CGFloat attachWidth(void *context) {
  return static_cast<CGFloat *>(context)[0];
}
void attachDealloc(void *context) {
  delete[] static_cast<CGFloat *>(context);
}

NSAttributedString *withInlineView(CGFloat width, CGFloat height, CGFloat fontSize = 16.0) {
  NSMutableAttributedString *string = [[NSMutableAttributedString alloc] init];
  NSDictionary *attributes = @{NSFontAttributeName : [NSFont systemFontOfSize:fontSize]};

  [string appendAttributedString:[[NSAttributedString alloc] initWithString:@"before "
                                                                attributes:attributes]];

  CTRunDelegateCallbacks callbacks = {};
  callbacks.version = kCTRunDelegateCurrentVersion;
  callbacks.dealloc = attachDealloc;
  callbacks.getAscent = attachAscent;
  callbacks.getDescent = attachDescent;
  callbacks.getWidth = attachWidth;
  auto *box = new CGFloat[2]{width, height};
  CTRunDelegateRef delegate = CTRunDelegateCreate(&callbacks, box);

  NSMutableDictionary *withDelegate = [attributes mutableCopy];
  withDelegate[(__bridge NSString *)kCTRunDelegateAttributeName] = (__bridge_transfer id)delegate;
  // U+FFFC, the object replacement character React Native uses for an attachment.
  [string appendAttributedString:[[NSAttributedString alloc] initWithString:@"\uFFFC"
                                                                attributes:withDelegate]];

  [string appendAttributedString:[[NSAttributedString alloc] initWithString:@" after"
                                                                attributes:attributes]];
  return string;
}

RnTextLayout *inlineLayout(CGFloat width, CGFloat height) {
  return [RnTextLayout layoutWithAttributedString:withInlineView(width, height)
                             maximumNumberOfLines:0
                                   truncationType:kCTLineTruncationEnd
                                        truncates:YES];
}

// "before " is 7 characters, so the attachment is at UTF-16 index 7.
constexpr NSUInteger kAttachmentIndex = 7;

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

// Inline views: `<Text>before <View/> after</Text>`, where a run delegate carries
// the size React Native measured for the view. The counterpart of the Pango
// tests on the other host. See backlog/text.md.

TEST(an_inline_view_reserves_its_own_width) {
  const CGSize narrow = [inlineLayout(10, 20) sizeForWidth:-1];
  const CGSize wide = [inlineLayout(120, 20) sizeForWidth:-1];

  EXPECT(wide.width > narrow.width);
  EXPECT(wide.width - narrow.width > 100);
}

TEST(a_tall_inline_view_makes_the_line_taller) {
  const CGSize shortOne = [inlineLayout(20, 8) sizeForWidth:-1];
  const CGSize tall = [inlineLayout(20, 90) sizeForWidth:-1];

  EXPECT(tall.height > shortOne.height);
}

TEST(an_inline_view_is_positioned_after_the_text_before_it) {
  const CGRect box = [inlineLayout(40, 20) frameForCharacterIndex:kAttachmentIndex width:-1];

  EXPECT(!CGRectIsNull(box));
  EXPECT(box.origin.x > 0);
}

TEST(an_inline_view_is_as_tall_as_it_asked_to_be) {
  const CGRect box = [inlineLayout(40, 40) frameForCharacterIndex:kAttachmentIndex width:-1];

  EXPECT(!CGRectIsNull(box));
  // Its own run's bounds, not the line's, which is why a view taller than the
  // text reports its own height rather than the text's.
  EXPECT(box.size.height >= 39);
  EXPECT(box.size.height <= 41);
}

TEST(a_wrapped_inline_view_sits_lower) {
  const CGRect wide = [inlineLayout(40, 20) frameForCharacterIndex:kAttachmentIndex width:-1];
  const CGRect wrapped = [inlineLayout(40, 20) frameForCharacterIndex:kAttachmentIndex width:60];

  EXPECT(!CGRectIsNull(wide));
  EXPECT(!CGRectIsNull(wrapped));
  EXPECT(wrapped.origin.y > wide.origin.y);
}

TEST(a_character_past_the_end_has_no_frame) {
  const CGRect box = [inlineLayout(40, 20) frameForCharacterIndex:9999 width:-1];
  EXPECT(CGRectIsNull(box));
}
