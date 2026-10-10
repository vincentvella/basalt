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

#import "CoreTextLayout.h"

#include "FontScaling.h"
#import "RnTextLayout.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <sstream>
#include <vector>

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

// The same paragraph under `ellipsizeMode: 'clip'`, which asks for the line
// limit and no ellipsis. `truncates:NO` is what CoreTextLayout.mm builds for it,
// and the truncation type it is given is then never consulted.
RnTextLayout *clippedLayoutFor(NSString *text, CGFloat size, NSInteger lines) {
  return [RnTextLayout layoutWithAttributedString:styled(text, size, NSTextAlignmentNatural)
                             maximumNumberOfLines:lines
                                   truncationType:kCTLineTruncationEnd
                                        truncates:NO];
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

// `numberOfLines` with `ellipsizeMode: 'clip'`: cut to the limit, with no
// ellipsis on the last line that stays.
//
// The reason to assert this on a host that already does it is that the line
// limit and the ellipsis are two separate decisions, and a text engine that
// only truncates in order to place an ellipsis puts them back together. Pango
// is exactly that engine -- a line limit there is a negative height it consults
// only while ellipsizing -- so the GTK side had to grow the cut by hand, and
// these are the tests that say Core Text must never acquire the same coupling.
// See backlog/text.md.

TEST(a_line_limit_applies_without_an_ellipsis_too) {
  @autoreleasepool {
    const CGSize unlimited = [layoutFor(kLong, 16, 0) sizeForWidth:200];
    const CGSize ellipsized = [layoutFor(kLong, 16, 2) sizeForWidth:200];
    const CGSize clipped = [clippedLayoutFor(kLong, 16, 2) sizeForWidth:200];

    // Shorter than the whole paragraph: the limit is honoured even though
    // nothing is being replaced by an ellipsis.
    EXPECT(clipped.height < unlimited.height);

    // And the same height as the ellipsizing one, because the two keep the same
    // two lines and differ only in what the second one ends with.
    EXPECT_NEAR(clipped.height, ellipsized.height, 1.0);
  }
}

TEST(clipping_keeps_exactly_the_number_of_lines_it_was_given) {
  @autoreleasepool {
    const CGSize one = [clippedLayoutFor(kLong, 16, 1) sizeForWidth:200];
    const CGSize two = [clippedLayoutFor(kLong, 16, 2) sizeForWidth:200];
    const CGSize three = [clippedLayoutFor(kLong, 16, 3) sizeForWidth:200];

    // Keeping one line too few or too many would still measure shorter than the
    // whole paragraph, which is what the test above checks, so the count itself
    // is asserted here.
    EXPECT(two.height > one.height);
    EXPECT(three.height > two.height);
    EXPECT_NEAR(two.height, one.height * 2, 2.0);
    EXPECT_NEAR(three.height, one.height * 3, 3.0);
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

TEST(an_inline_view_below_a_clip_has_no_frame) {
  @autoreleasepool {
    // At width 60 "before " fills the first line and the box is pushed onto the
    // second, which is what `a_wrapped_inline_view_sits_lower` above relies on.
    // A one-line limit then hides that second line, so there is no position to
    // report and CoreTextLayoutManager turns the null rect into `isClipped`.
    // That is the one place a line limit without an ellipsis is visible to
    // React Native rather than only to the eye.
    RnTextLayout *clipped = [RnTextLayout layoutWithAttributedString:withInlineView(40, 20)
                                               maximumNumberOfLines:1
                                                     truncationType:kCTLineTruncationEnd
                                                          truncates:NO];

    const CGRect hidden = [clipped frameForCharacterIndex:kAttachmentIndex width:60];
    EXPECT(CGRectIsNull(hidden));

    // The same paragraph with room for both lines does place it, so the null
    // above is the limit at work and not the query failing.
    const CGRect placed = [inlineLayout(40, 20) frameForCharacterIndex:kAttachmentIndex width:60];
    EXPECT(!CGRectIsNull(placed));
  }
}

// `textAlign: 'justify'`, which works and was recorded as not working.
//
// backlog/platform-macos.md said Core Text ignores justification for lines drawn
// one at a time, which is true and is not what this draws: `linesForWidth:`
// takes its lines out of a `CTFrame`, and Core Text justifies the lines in a
// frame itself. So the prop has been honoured all along and nothing asserted it.
//
// Measured as the rightmost column with ink in it: a justified line reaches the
// right edge of the box, and the same text drawn flush left stops short of it --
// 199 against 186 in a 200 point box, which is also the evidence that struck
// that entry.
TEST(text_justified_lines_reach_both_edges) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(200, 80);
    // Long enough to wrap to three lines in 200 points, with spaces to stretch.
    NSString *paragraph = @"Justified text stretches every line but the last one";

    const auto inkEdges = [&](NSTextAlignment alignment, int row) {
      RnTextLayout *layout =
          [RnTextLayout layoutWithAttributedString:styled(paragraph, 14, alignment)
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
      // The bitmap counts y from the bottom and the layout draws from the top, so
      // the first drawn line is the last band of rows.
      const size_t band = (size_t)size.height - (size_t)row * 20 - 10;
      int rightmost = -1;
      for (size_t x = 0; x < (size_t)size.width; x++) {
        for (size_t y = band - 8; y < band + 8; y++) {
          if (pixels[y * stride + x * 4] < 200) {
            rightmost = (int)x;
            break;
          }
        }
      }
      CGContextRelease(context);
      return rightmost;
    };

    // The first line, which is justified.
    const int flushLeft = inkEdges(NSTextAlignmentNatural, 0);
    const int justified = inkEdges(NSTextAlignmentJustified, 0);
    EXPECT(flushLeft > 0);
    // Stretched to the edge: within a couple of points of 200, and further right
    // than the same line left-aligned.
    EXPECT(justified > flushLeft);
    EXPECT(justified >= 195);
  }
}

// And the last line is left alone, which is what justification means
// typographically and what Pango does on the other host -- Core Text agrees
// without being asked, which is the other half of this being already done.
//
// The lines are found rather than assumed: the paragraph's own wrapping decides
// how many there are, so this collects the rightmost ink per row of the bitmap,
// groups the rows that have any into bands, and asks about the first band and
// the last.
TEST(text_the_last_justified_line_is_not_stretched) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(200, 80);
    NSString *paragraph = @"Justified text stretches every line but the last one";

    RnTextLayout *layout =
        [RnTextLayout layoutWithAttributedString:styled(paragraph, 14, NSTextAlignmentJustified)
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

    // The rightmost inked column in each row, or -1 for a blank row. The bitmap
    // counts y from the bottom, so the first drawn line is at the high end.
    std::vector<int> rightmostByRow((size_t)size.height, -1);
    for (size_t y = 0; y < (size_t)size.height; y++) {
      for (size_t x = (size_t)size.width; x > 0; x--) {
        if (pixels[y * stride + (x - 1) * 4] < 200) {
          rightmostByRow[y] = (int)(x - 1);
          break;
        }
      }
    }
    CGContextRelease(context);

    // Group the inked rows into bands, one per drawn line, and take each band's
    // furthest ink.
    std::vector<int> bands;
    bool inBand = false;
    int furthest = -1;
    for (size_t y = (size_t)size.height; y > 0; y--) {
      const int rightmost = rightmostByRow[y - 1];
      if (rightmost >= 0) {
        inBand = true;
        furthest = std::max(furthest, rightmost);
      } else if (inBand) {
        bands.push_back(furthest);
        inBand = false;
        furthest = -1;
      }
    }
    if (inBand) {
      bands.push_back(furthest);
    }

    // Three lines in this box, and in any case more than one: a single line
    // would make the question meaningless, the first line being the last.
    EXPECT(bands.size() >= 2);
    // The first is stretched to the edge and the last is not. A host that
    // justified every line would push the last one to 200 as well, and the
    // paragraph would read as a bug rather than as a choice.
    EXPECT(bands.front() >= 195);
    EXPECT(bands.back() < 190);
  }
}

// `textTransform`, the same four questions the GTK suite asks, over the same
// cases: the two hosts use their own toolkit's Unicode case mapping -- NSString's
// here, GLib's there -- and the point of asking twice is that they agree.
namespace {

std::string transformedText(const std::string &text, facebook::react::TextTransform transform) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 16.0F;
  attributes.textTransform = transform;

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;

  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));

  RnTextLayout *layout = basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{});
  NSString *laid = layout.attributedString.string;
  return laid != nil ? std::string(laid.UTF8String) : std::string();
}

} // namespace

TEST(appkit_text_transform_uppercases_and_lowercases) {
  @autoreleasepool {
    using facebook::react::TextTransform;
    EXPECT_EQ(transformedText("shout", TextTransform::Uppercase), std::string("SHOUT"));
    EXPECT_EQ(transformedText("WHISPER", TextTransform::Lowercase), std::string("whisper"));
    EXPECT_EQ(transformedText("As Written", TextTransform::None), std::string("As Written"));
  }
}

TEST(appkit_text_transform_knows_unicode_rather_than_bytes) {
  @autoreleasepool {
    using facebook::react::TextTransform;
    EXPECT_EQ(transformedText("café", TextTransform::Uppercase), std::string("CAFÉ"));
    // One character in, two out, which a byte-wise transform cannot do.
    EXPECT_EQ(transformedText("straße", TextTransform::Uppercase), std::string("STRASSE"));
  }
}

TEST(appkit_text_transform_capitalize_follows_react_natives_rule) {
  @autoreleasepool {
    using facebook::react::TextTransform;
    EXPECT_EQ(transformedText("hello wide world", TextTransform::Capitalize),
              std::string("Hello Wide World"));
    EXPECT_EQ(transformedText("iOS and Android", TextTransform::Capitalize),
              std::string("Ios And Android"));
    EXPECT_EQ(transformedText("3rd place", TextTransform::Capitalize), std::string("3rd Place"));
  }
}

// And it reaches the measurement, not only the drawing: "shout" and "SHOUT" are
// different widths, so a host that transformed at paint time would wrap in the
// wrong place.
TEST(appkit_text_transform_changes_what_the_paragraph_measures) {
  @autoreleasepool {
    using facebook::react::TextTransform;
    const auto widthOf = [](facebook::react::TextTransform transform) {
      facebook::react::TextAttributes attributes;
      attributes.fontSize = 16.0F;
      attributes.textTransform = transform;
      facebook::react::AttributedString::Fragment fragment;
      fragment.string = "shout";
      fragment.textAttributes = attributes;
      facebook::react::AttributedString string;
      string.appendFragment(std::move(fragment));

      RnTextLayout *layout =
          basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{});
      return [layout sizeForWidth:-1].width;
    };

    EXPECT(widthOf(TextTransform::Uppercase) > widthOf(TextTransform::None));
  }
}

// `textShadowColor`, `textShadowOffset` and `textShadowRadius`, against a real
// bitmap.
//
// Which fragment's shadow wins is core's and is tested there. What only a
// picture can say is that the shadow is drawn at all -- Core Text ignores an
// `NSShadow` attribute, so this one is set on the context -- and that its offset
// goes the way React Native means: the drawing flips the context back to Core
// Text's y-up orientation, so a positive offset has to be negated on the way in
// or the shadow lands above the glyphs instead of below them.
TEST(text_a_shadow_is_drawn_where_react_native_offsets_it) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(200, 100);

    // The red channel of every pixel, so a red shadow and black glyphs on white
    // are told apart by their green instead.
    const auto draw = [&](CGFloat dx, CGFloat dy, NSColor *shadowColor) {
      RnTextLayout *layout =
          [RnTextLayout layoutWithAttributedString:styled(@"H", 48, NSTextAlignmentLeft)
                              maximumNumberOfLines:0
                                    truncationType:kCTLineTruncationEnd
                                         truncates:YES];
      layout.shadowOffset = CGSizeMake(dx, dy);
      layout.shadowStandardDeviation = 0;
      layout.shadowColor = shadowColor;

      CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height,
                                                   8, 0, space, kCGImageAlphaPremultipliedLast);
      CGColorSpaceRelease(space);
      CGContextSetRGBFillColor(context, 1, 1, 1, 1);
      CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
      [layout drawInContext:context size:size];
      return context;
    };

    // Where the ink is without a shadow, measured rather than assumed: the font
    // is whatever this machine has.
    CGContextRef plain = draw(0, 0, nil);
    auto *plainPixels = static_cast<unsigned char *>(CGBitmapContextGetData(plain));
    const size_t stride = CGBitmapContextGetBytesPerRow(plain);
    int left = (int)size.width;
    int right = -1;
    int top = (int)size.height;
    int bottom = -1;
    for (int x = 0; x < (int)size.width; x++) {
      for (int y = 0; y < (int)size.height; y++) {
        if (plainPixels[y * stride + x * 4] < 200) {
          left = std::min(left, x);
          right = std::max(right, x);
          top = std::min(top, y);
          bottom = std::max(bottom, y);
        }
      }
    }
    CGContextRelease(plain);
    EXPECT(right > left);
    EXPECT(bottom > top);

    // A hard red shadow twenty points right and ten down. The part past the
    // glyph's own right edge is where nothing was drawn before, and the rows
    // *below* the glyph are what say the sign survived the flip.
    NSColor *red = [NSColor colorWithSRGBRed:1 green:0 blue:0 alpha:1];
    CGContextRef shadowed = draw(20, 10, red);
    auto *pixels = static_cast<unsigned char *>(CGBitmapContextGetData(shadowed));

    const auto redAt = [&](int x, int y) {
      const unsigned char *pixel = &pixels[(size_t)y * stride + (size_t)x * 4];
      return pixel[0] > 150 && pixel[1] < 120 && pixel[2] < 120;
    };

    int redPastTheGlyph = 0;
    int redBelowTheGlyph = 0;
    int redAboveTheGlyph = 0;
    for (int x = right + 1; x <= right + 19 && x < (int)size.width; x++) {
      for (int y = top; y <= bottom; y++) {
        if (redAt(x, y)) {
          redPastTheGlyph++;
        }
      }
    }
    for (int x = left; x <= right; x++) {
      for (int y = bottom + 1; y <= bottom + 9 && y < (int)size.height; y++) {
        if (redAt(x, y)) {
          redBelowTheGlyph++;
        }
      }
      for (int y = std::max(top - 9, 0); y < top; y++) {
        if (redAt(x, y)) {
          redAboveTheGlyph++;
        }
      }
    }
    CGContextRelease(shadowed);

    EXPECT(redPastTheGlyph > 0);
    // Down the screen, which is what a positive offset means to React Native.
    EXPECT(redBelowTheGlyph > 0);
    // And not up it, which is what the unflipped sign would have done.
    EXPECT_EQ(redAboveTheGlyph, 0);
  }
}

// No colour is no shadow, and taking one away takes the shadow with it: a
// context shadow left set would shadow everything drawn after the text.
TEST(text_no_shadow_colour_is_no_shadow) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(200, 100);
    RnTextLayout *layout =
        [RnTextLayout layoutWithAttributedString:styled(@"H", 48, NSTextAlignmentLeft)
                            maximumNumberOfLines:0
                                  truncationType:kCTLineTruncationEnd
                                       truncates:YES];
    layout.shadowOffset = CGSizeMake(20, 10);
    layout.shadowStandardDeviation = 4;
    // No colour, which is React Native's default for every <Text>.
    EXPECT(layout.shadowColor == nil);

    CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height,
                                                 8, 0, space, kCGImageAlphaPremultipliedLast);
    CGColorSpaceRelease(space);
    CGContextSetRGBFillColor(context, 1, 1, 1, 1);
    CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
    [layout drawInContext:context size:size];

    // A rectangle drawn after the paragraph, which would pick up a context
    // shadow that the drawing had left behind.
    CGContextSetRGBFillColor(context, 0, 0, 1, 1);
    CGContextFillRect(context, CGRectMake(120, 60, 20, 20));

    auto *pixels = static_cast<unsigned char *>(CGBitmapContextGetData(context));
    const size_t stride = CGBitmapContextGetBytesPerRow(context);
    // Just outside the blue rectangle: white, not a shadow of it.
    const unsigned char *beside = &pixels[(size_t)70 * stride + (size_t)145 * 4];
    EXPECT(beside[0] > 240);
    EXPECT(beside[1] > 240);
    EXPECT(beside[2] > 240);
    CGContextRelease(context);
  }
}

// The text scale, and the two props that decide whether a fragment honours it.
//
// The same question the GTK suite asks, spelled the same way, because the rule
// is core's and the only thing that differs is which font the scale reaches.
// macOS reports no scale of its own -- appKitTextScale() says why -- so what is
// asserted here is that the host passes whatever it is given through to the
// font, which is what makes the two props mean anything on this platform.
namespace {

CGFloat pointSizeAt(float scale, std::optional<bool> allowFontScaling,
                    double maxFontSizeMultiplier) {
  basalt::setSystemFontScale(scale);

  facebook::react::TextAttributes attributes;
  attributes.fontSize = 16.0F;
  attributes.allowFontScaling = allowFontScaling;
  attributes.maxFontSizeMultiplier = maxFontSizeMultiplier;

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = "scaled";
  fragment.textAttributes = attributes;

  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));

  RnTextLayout *layout = basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{});
  NSFont *font = [layout.attributedString attribute:NSFontAttributeName
                                            atIndex:0
                                     effectiveRange:nullptr];
  basalt::setSystemFontScale(1.0F);
  return font != nil ? font.pointSize : 0.0;
}

} // namespace

TEST(appkit_a_text_scale_reaches_the_font) {
  @autoreleasepool {
    EXPECT(std::fabs(pointSizeAt(1.0F, std::nullopt, std::nan("")) - 16.0) < 0.01);
    EXPECT(std::fabs(pointSizeAt(1.5F, std::nullopt, std::nan("")) - 24.0) < 0.01);
  }
}

TEST(appkit_allow_font_scaling_false_keeps_the_size) {
  @autoreleasepool {
    EXPECT(std::fabs(pointSizeAt(1.5F, false, std::nan("")) - 16.0) < 0.01);
    EXPECT(std::fabs(pointSizeAt(1.5F, true, std::nan("")) - 24.0) < 0.01);
  }
}

TEST(appkit_max_font_size_multiplier_is_a_ceiling) {
  @autoreleasepool {
    EXPECT(std::fabs(pointSizeAt(2.0F, std::nullopt, 1.25) - 20.0) < 0.01);
    // And it is upstream's rule that a ceiling under 1 is no ceiling at all.
    EXPECT(std::fabs(pointSizeAt(2.0F, std::nullopt, 0.5) - 32.0) < 0.01);
  }
}

TEST(appkit_text_scale_is_one_because_macos_publishes_none) {
  EXPECT(std::fabs(basalt::appKitTextScale() - 1.0F) < 0.001F);
}

// `textDecorationColor` and `textDecorationStyle`, the pair the GTK suite asks
// about in its own terms. Both read through `buildTextLayout`, so what is
// asserted is what Core Text was handed, and then that it drew it.
namespace {

NSAttributedString *decorated(
    std::optional<facebook::react::TextDecorationStyle> style,
    bool coloured,
    facebook::react::TextDecorationLineType line =
        facebook::react::TextDecorationLineType::Underline) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 24.0F;
  attributes.foregroundColor = facebook::react::colorFromComponents(
      facebook::react::ColorComponents{0.0F, 0.0F, 0.0F, 1.0F});
  attributes.textDecorationLineType = line;
  attributes.textDecorationStyle = style;
  if (coloured) {
    attributes.textDecorationColor = facebook::react::colorFromComponents(
        facebook::react::ColorComponents{1.0F, 0.0F, 0.0F, 1.0F});
  }

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = "Hxy";
  fragment.textAttributes = attributes;
  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));

  return basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{}).attributedString;
}

} // namespace

TEST(appkit_a_decoration_style_maps_to_core_texts_bitmask) {
  @autoreleasepool {
    using facebook::react::TextDecorationStyle;
    const auto underline = [](std::optional<TextDecorationStyle> style) {
      NSNumber *value = [decorated(style, false) attribute:NSUnderlineStyleAttributeName
                                                  atIndex:0
                                           effectiveRange:nullptr];
      return value == nil ? NSInteger(-1) : value.integerValue;
    };

    EXPECT_EQ(underline(std::nullopt), NSInteger(NSUnderlineStyleSingle));
    EXPECT_EQ(underline(TextDecorationStyle::Solid), NSInteger(NSUnderlineStyleSingle));
    EXPECT_EQ(underline(TextDecorationStyle::Double), NSInteger(NSUnderlineStyleDouble));
    // The pattern rides in the same bitmask, which is what AppKit has and Pango
    // does not.
    EXPECT_EQ(underline(TextDecorationStyle::Dotted),
              NSInteger(NSUnderlineStyleSingle | NSUnderlineStylePatternDot));
    EXPECT_EQ(underline(TextDecorationStyle::Dashed),
              NSInteger(NSUnderlineStyleSingle | NSUnderlineStylePatternDash));
    // And the one that is exact on GTK and not here: no wavy pattern exists, so
    // it is a single line and recorded as such.
    EXPECT_EQ(underline(TextDecorationStyle::Wavy), NSInteger(NSUnderlineStyleSingle));
  }
}

TEST(appkit_a_strikethrough_takes_the_same_style_and_colour) {
  @autoreleasepool {
    NSAttributedString *string =
        decorated(facebook::react::TextDecorationStyle::Dotted, true,
                  facebook::react::TextDecorationLineType::UnderlineStrikethrough);
    NSNumber *strike = [string attribute:NSStrikethroughStyleAttributeName
                                atIndex:0
                         effectiveRange:nullptr];
    EXPECT(strike != nil);
    EXPECT(strike != nil
           && strike.integerValue == NSInteger(NSUnderlineStyleSingle | NSUnderlineStylePatternDot));
    EXPECT([string attribute:NSStrikethroughColorAttributeName
                    atIndex:0
             effectiveRange:nullptr] != nil);
  }
}

// An unset colour is the text's own, which means *no* attribute: setting one
// would be this platform choosing a colour React Native did not.
TEST(appkit_an_unset_decoration_colour_sets_no_attribute) {
  @autoreleasepool {
    EXPECT([decorated(std::nullopt, false) attribute:NSUnderlineColorAttributeName
                                            atIndex:0
                                     effectiveRange:nullptr] == nil);
    EXPECT([decorated(std::nullopt, true) attribute:NSUnderlineColorAttributeName
                                           atIndex:0
                                    effectiveRange:nullptr] != nil);
  }
}

// And it reaches the page. Black text, a red underline, and a red pixel is one
// the glyphs cannot have drawn.
TEST(appkit_a_decoration_colour_reaches_the_underline) {
  @autoreleasepool {
    const CGSize size = CGSizeMake(200, 60);
    const auto redPixels = [&](bool coloured) {
      RnTextLayout *layout = [RnTextLayout
          layoutWithAttributedString:decorated(std::nullopt, coloured)
                maximumNumberOfLines:0
                      truncationType:kCTLineTruncationEnd
                           truncates:YES];
      CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width,
                                                   (size_t)size.height, 8, 0, space,
                                                   kCGImageAlphaPremultipliedLast);
      CGColorSpaceRelease(space);
      CGContextSetRGBFillColor(context, 1, 1, 1, 1);
      CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
      [layout drawInContext:context size:size];

      auto *pixels = static_cast<unsigned char *>(CGBitmapContextGetData(context));
      const size_t stride = CGBitmapContextGetBytesPerRow(context);
      int red = 0;
      for (size_t y = 0; y < (size_t)size.height; y++) {
        for (size_t x = 0; x < (size_t)size.width; x++) {
          const unsigned char *pixel = pixels + y * stride + x * 4;
          if (pixel[0] > 150 && pixel[1] < 110 && pixel[2] < 110) {
            red++;
          }
        }
      }
      CGContextRelease(context);
      return red;
    };

    // The negative control: black text with an uncoloured underline puts no red
    // on the page at all.
    EXPECT_EQ(redPixels(false), 0);
    EXPECT(redPixels(true) > 0);
  }
}

// `fontVariant`, in Core Text's vocabulary rather than OpenType's.
//
// What is asserted is the feature settings on the font, not a picture: whether
// `smcp` *changes* anything depends on the font having a small-caps table, and
// the font here is whatever the machine has. The AAT pair is this platform's
// half of the job and is where a wrong number would hide.
namespace {

// The mapping, not what a font kept of it: `[NSFont fontWithDescriptor:]`
// drops features the resolved font does not have, and the system font has
// neither oldstyle figures nor twenty stylistic sets. Asking the font would be
// asking this machine's font catalogue.
NSArray *featuresOf(std::optional<facebook::react::FontVariant> variant) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 20.0F;
  attributes.fontVariant = variant;
  return basalt::fontFeaturesFor(attributes);
}

// And the half that only a real font can answer: a feature the system font does
// have has to survive onto the font the layout uses.
NSFont *fontWith(facebook::react::FontVariant variant) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 20.0F;
  attributes.fontVariant = variant;

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = "Figures 123";
  fragment.textAttributes = attributes;
  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));

  return [basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{})
                 .attributedString attribute:NSFontAttributeName
                                    atIndex:0
                             effectiveRange:nullptr];
}

bool hasFeature(NSArray *features, int type, int selector) {
  for (NSDictionary *feature in features) {
    if ([feature[NSFontFeatureTypeIdentifierKey] intValue] == type
        && [feature[NSFontFeatureSelectorIdentifierKey] intValue] == selector) {
      return true;
    }
  }
  return false;
}

} // namespace

TEST(appkit_a_font_variant_becomes_core_text_feature_settings) {
  @autoreleasepool {
    using facebook::react::FontVariant;
    // Nothing asked means no settings at all, rather than an empty array that
    // would rebuild the font for nothing.
    EXPECT(featuresOf(std::nullopt) == nil || [featuresOf(std::nullopt) count] == 0);

    NSArray *smallCaps = featuresOf(FontVariant::SmallCaps);
    EXPECT(smallCaps != nil && smallCaps.count == 1);
    EXPECT(hasFeature(smallCaps, kLowerCaseType, kLowerCaseSmallCapsSelector));

    // The two number cases are the same AAT type and differ by selector, which
    // is the pair most likely to be swapped.
    EXPECT(hasFeature(featuresOf(FontVariant::OldstyleNums), kNumberCaseType,
                      kLowerCaseNumbersSelector));
    EXPECT(hasFeature(featuresOf(FontVariant::LiningNums), kNumberCaseType,
                      kUpperCaseNumbersSelector));
    EXPECT(hasFeature(featuresOf(FontVariant::TabularNums), kNumberSpacingType,
                      kMonospacedNumbersSelector));
    EXPECT(hasFeature(featuresOf(FontVariant::ProportionalNums), kNumberSpacingType,
                      kProportionalNumbersSelector));

    NSArray *both = featuresOf(static_cast<FontVariant>(
        static_cast<int>(FontVariant::SmallCaps) | static_cast<int>(FontVariant::TabularNums)));
    EXPECT(both != nil && both.count == 2);

    // And that it reaches a real font, which the mapping alone cannot say.
    // Small caps because the system font has them; a feature it lacks would be
    // dropped by Core Text and prove nothing either way.
    NSArray *kept =
        [fontWith(FontVariant::SmallCaps).fontDescriptor objectForKey:NSFontFeatureSettingsAttribute];
    EXPECT(kept != nil && hasFeature(kept, kLowerCaseType, kLowerCaseSmallCapsSelector));
    // A fragment that asked for nothing is left on the plain font.
    EXPECT([fontWith(FontVariant::Default).fontDescriptor
               objectForKey:NSFontFeatureSettingsAttribute] == nil);
  }
}

// The stylistic alternates are a formula rather than twenty table entries, so
// the formula is checked against the SDK's own constants rather than against
// the comment that states it.
TEST(appkit_a_stylistic_alternate_selector_is_twice_its_number) {
  @autoreleasepool {
    using facebook::react::FontVariant;
    EXPECT_EQ(kStylisticAltOneOnSelector, 1 * 2);
    EXPECT_EQ(kStylisticAltTwentyOnSelector, 20 * 2);
    EXPECT(hasFeature(featuresOf(FontVariant::StylisticOne), kStylisticAlternativesType,
                      kStylisticAltOneOnSelector));
    EXPECT(hasFeature(featuresOf(FontVariant::StylisticSeven), kStylisticAlternativesType,
                      kStylisticAltSevenOnSelector));
    EXPECT(hasFeature(featuresOf(FontVariant::StylisticTwenty), kStylisticAlternativesType,
                      kStylisticAltTwentyOnSelector));
  }
}

// `TextAttributes::opacity`, which multiplies the alpha of both colours.
TEST(appkit_a_fragment_opacity_multiplies_the_colour_alpha) {
  @autoreleasepool {
    const auto alphaOf = [](double opacity, bool background) {
      facebook::react::TextAttributes attributes;
      attributes.fontSize = 20.0F;
      attributes.opacity = opacity;
      attributes.foregroundColor = facebook::react::colorFromComponents(
          facebook::react::ColorComponents{1.0F, 0.0F, 0.0F, 1.0F});
      if (background) {
        attributes.backgroundColor = facebook::react::colorFromComponents(
            facebook::react::ColorComponents{0.0F, 0.0F, 1.0F, 1.0F});
      }

      facebook::react::AttributedString::Fragment fragment;
      fragment.string = "half";
      fragment.textAttributes = attributes;
      facebook::react::AttributedString string;
      string.appendFragment(std::move(fragment));

      NSAttributedString *laid =
          basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{}).attributedString;
      NSColor *color = [laid attribute:(background ? NSBackgroundColorAttributeName
                                                   : NSForegroundColorAttributeName)
                              atIndex:0
                       effectiveRange:nullptr];
      return color == nil ? -1.0 : (double)color.alphaComponent;
    };

    EXPECT(std::fabs(alphaOf(1.0, false) - 1.0) < 0.01);
    EXPECT(std::fabs(alphaOf(0.5, false) - 0.5) < 0.01);
    // Both colours, which is upstream's rule and not only the foreground.
    EXPECT(std::fabs(alphaOf(0.25, true) - 0.25) < 0.01);
  }
}

// `baseWritingDirection`, which decides which edge a paragraph starts from.
//
// The GTK suite asks the same two questions: that the engine was told, and that
// the ink moved. The second matters more than it looks -- on GTK the direction
// reached Pango and the glyphs stayed against the left edge, because Pango
// flips a natural alignment only when it is deciding the direction itself.
// `NSTextAlignmentNatural` has no such condition, which is what this confirms.
namespace {

NSAttributedString *directed(std::optional<facebook::react::WritingDirection> direction) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = 24.0F;
  attributes.foregroundColor = facebook::react::colorFromComponents(
      facebook::react::ColorComponents{0.0F, 0.0F, 0.0F, 1.0F});
  attributes.baseWritingDirection = direction;

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = "abc";
  fragment.textAttributes = attributes;
  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));

  return basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{}).attributedString;
}

} // namespace

TEST(appkit_a_base_writing_direction_reaches_the_paragraph_style) {
  @autoreleasepool {
    using facebook::react::WritingDirection;
    const auto directionOf = [](std::optional<WritingDirection> asked) {
      NSParagraphStyle *style = [directed(asked) attribute:NSParagraphStyleAttributeName
                                                  atIndex:0
                                           effectiveRange:nullptr];
      return style == nil ? NSWritingDirectionNatural : style.baseWritingDirection;
    };

    EXPECT_EQ(directionOf(WritingDirection::RightToLeft), NSWritingDirectionRightToLeft);
    EXPECT_EQ(directionOf(WritingDirection::LeftToRight), NSWritingDirectionLeftToRight);
    // Natural is set rather than skipped: a nested <Text> inherits this style,
    // and an unset direction would let the enclosing paragraph's stand where
    // the app asked for the algorithm's answer.
    EXPECT_EQ(directionOf(WritingDirection::Natural), NSWritingDirectionNatural);
  }
}

TEST(appkit_a_right_to_left_paragraph_draws_against_the_right_edge) {
  @autoreleasepool {
    using facebook::react::WritingDirection;
    const CGSize size = CGSizeMake(200, 60);

    const auto inkCentre = [&](std::optional<WritingDirection> asked) {
      RnTextLayout *layout = [RnTextLayout layoutWithAttributedString:directed(asked)
                                                maximumNumberOfLines:0
                                                      truncationType:kCTLineTruncationEnd
                                                           truncates:YES];
      CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
      CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width,
                                                   (size_t)size.height, 8, 0, space,
                                                   kCGImageAlphaPremultipliedLast);
      CGColorSpaceRelease(space);
      CGContextSetRGBFillColor(context, 1, 1, 1, 1);
      CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
      [layout drawInContext:context size:size];

      auto *pixels = static_cast<unsigned char *>(CGBitmapContextGetData(context));
      const size_t stride = CGBitmapContextGetBytesPerRow(context);
      long total = 0;
      long weight = 0;
      for (size_t y = 0; y < (size_t)size.height; y++) {
        for (size_t x = 0; x < (size_t)size.width; x++) {
          if (pixels[y * stride + x * 4] < 200) {  // any ink
            total += (long)x;
            weight++;
          }
        }
      }
      CGContextRelease(context);
      return weight > 0 ? (double)total / (double)weight : -1.0;
    };

    const double ltr = inkCentre(WritingDirection::LeftToRight);
    const double rtl = inkCentre(WritingDirection::RightToLeft);
    EXPECT(ltr >= 0.0);
    EXPECT(rtl >= 0.0);
    EXPECT(ltr < 100.0);
    EXPECT(rtl > 100.0);
  }
}

// --- adjustsFontSizeToFit ----------------------------------------------------
//
// The search is core's and tested there; what these ask is the half that is
// Core Text's -- that the ratio reaches the font sizes, that the fitted
// paragraph actually fits, and that a paragraph which never asked is untouched.
namespace {

facebook::react::AttributedString label(const std::string &text, float fontSize) {
  facebook::react::TextAttributes attributes;
  attributes.fontSize = fontSize;

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;

  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));
  return string;
}

} // namespace

TEST(appkit_text_adjusts_font_size_to_fit_shrinks_until_it_fits) {
  @autoreleasepool {
    const auto text = label("Delete all of the messages", 32.0F);

    facebook::react::ParagraphAttributes fitting;
    fitting.adjustsFontSizeToFit = true;
    const basalt::FontFit fit = basalt::textFitScale(text, fitting, 200.0F, 40.0F);
    EXPECT(fit.scales());
    EXPECT(fit.ratio < 1.0);

    // And the fitted paragraph fits, measured through the same builder the host
    // paints with.
    RnTextLayout *layout = basalt::buildTextLayout(text, fitting, fit);
    const CGSize size = [layout sizeForWidth:200.0];
    EXPECT(size.width <= 200.0);
    EXPECT(size.height <= 40.0);
  }
}

TEST(appkit_text_a_paragraph_that_did_not_ask_is_not_shrunk) {
  @autoreleasepool {
    const auto text = label("Delete all of the messages", 32.0F);
    facebook::react::ParagraphAttributes plain;
    EXPECT(!basalt::textFitScale(text, plain, 200.0F, 40.0F).scales());

    RnTextLayout *layout = basalt::buildTextLayout(text, plain);
    // Wrapped and taller than the box, rather than shrunk into it.
    EXPECT([layout sizeForWidth:200.0].height > 40.0);
  }
}

TEST(appkit_text_a_paragraph_that_already_fits_keeps_its_size) {
  @autoreleasepool {
    facebook::react::ParagraphAttributes fitting;
    fitting.adjustsFontSizeToFit = true;
    EXPECT(!basalt::textFitScale(label("Hi", 16.0F), fitting, 300.0F, 100.0F).scales());
  }
}

TEST(appkit_text_the_minimum_font_size_stops_the_shrinking) {
  @autoreleasepool {
    facebook::react::ParagraphAttributes fitting;
    fitting.adjustsFontSizeToFit = true;
    fitting.minimumFontSize = 24.0F;
    const basalt::FontFit fit =
        basalt::textFitScale(label("Delete all of the messages", 32.0F), fitting, 60.0F, 30.0F);
    EXPECT_NEAR(fit.apply(32.0), 24.0, 0.0001);
  }
}

TEST(appkit_text_a_line_limit_does_not_hide_the_overflow_from_the_search) {
  @autoreleasepool {
    // `numberOfLines` with `adjustsFontSizeToFit` is the pairing a button label
    // is written with, and the reason the search measures the *untruncated*
    // paragraph: a one-line limit makes a layout that fits any box by throwing
    // text away.
    facebook::react::ParagraphAttributes oneLine;
    oneLine.adjustsFontSizeToFit = true;
    oneLine.maximumNumberOfLines = 1;
    const basalt::FontFit fit =
        basalt::textFitScale(label("Delete all of the messages", 32.0F), oneLine, 200.0F, 40.0F);
    EXPECT(fit.scales());
    EXPECT(fit.ratio < 1.0);
  }
}


// --- Which edge the text sits against -----------------------------------------
//
// The same table the GTK suite asserts, measured the same way -- the leftmost
// column with ink in it, in a 300pt box -- because the claim is that the two
// hosts agree. `textAlign`'s `start` and `end` are relative to the paragraph's
// direction, and this host had four of the eight rows wrong: it folded `left`
// into `natural`, folded `end` into `right`, and never resolved the direction
// of a paragraph whose text is right-to-left but whose prop said nothing. See
// core/TextAlignments.h for what each host used to answer.

namespace {

const char *const kHebrewText = "\xd7\xa9\xd7\x9c\xd7\x95\xd7\x9d \xd7\xa2\xd7\x95\xd7\x9c\xd7\x9d";

// The leftmost column holding ink, with the paragraph drawn into a 300pt box.
// Flush left is 0 or 1; flush right is past 200.
int inkStartsAt(const char *text,
                std::optional<facebook::react::WritingDirection> direction,
                std::optional<facebook::react::TextAlignment> alignment) {
  const CGSize size = CGSizeMake(300, 40);

  facebook::react::TextAttributes attributes;
  attributes.fontSize = 16.0F;
  attributes.alignment = alignment;
  attributes.baseWritingDirection = direction;

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;
  facebook::react::AttributedString string;
  string.appendFragment(std::move(fragment));

  RnTextLayout *layout = basalt::buildTextLayout(string, facebook::react::ParagraphAttributes{});

  CGColorSpaceRef space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
  CGContextRef context = CGBitmapContextCreate(nullptr, (size_t)size.width, (size_t)size.height, 8,
                                               0, space, kCGImageAlphaPremultipliedLast);
  CGColorSpaceRelease(space);
  CGContextSetRGBFillColor(context, 1, 1, 1, 1);
  CGContextFillRect(context, CGRectMake(0, 0, size.width, size.height));
  [layout drawInContext:context size:size];

  auto *pixels = static_cast<unsigned char *>(CGBitmapContextGetData(context));
  const size_t stride = CGBitmapContextGetBytesPerRow(context);
  int leftmost = -1;
  for (size_t x = 0; x < (size_t)size.width && leftmost < 0; x++) {
    for (size_t y = 0; y < (size_t)size.height; y++) {
      if (pixels[y * stride + x * 4] < 200) {
        leftmost = (int)x;
        break;
      }
    }
  }
  CGContextRelease(context);
  return leftmost;
}

bool drawnLeft(int x) {
  return x >= 0 && x < 5;
}

bool drawnRight(int x) {
  return x > 200;
}

} // namespace

TEST(appkit_text_align_left_and_right_are_physical_in_both_directions) {
  @autoreleasepool {
    using A = facebook::react::TextAlignment;
    using D = facebook::react::WritingDirection;

    EXPECT(drawnLeft(inkStartsAt("hello world", std::nullopt, A::Left)));
    EXPECT(drawnRight(inkStartsAt("hello world", std::nullopt, A::Right)));

    // The row this host had wrong: `left` was folded into `natural`, so a
    // right-to-left paragraph asking for the left edge drew flush right.
    EXPECT(drawnLeft(inkStartsAt(kHebrewText, D::RightToLeft, A::Left)));
    EXPECT(drawnRight(inkStartsAt(kHebrewText, D::RightToLeft, A::Right)));
    EXPECT(drawnLeft(inkStartsAt(kHebrewText, std::nullopt, A::Left)));
    EXPECT(drawnRight(inkStartsAt(kHebrewText, std::nullopt, A::Right)));
  }
}

TEST(appkit_text_align_natural_follows_the_paragraph_direction) {
  @autoreleasepool {
    using A = facebook::react::TextAlignment;
    using D = facebook::react::WritingDirection;

    EXPECT(drawnLeft(inkStartsAt("hello world", std::nullopt, A::Natural)));
    EXPECT(drawnLeft(inkStartsAt("hello world", std::nullopt, std::nullopt)));

    // Resolved from the text, which is what `natural` means and which Core Text
    // only does inside a frame -- and this layer draws its own lines, so a
    // paragraph of Hebrew with nothing set drew flush left.
    EXPECT(drawnRight(inkStartsAt(kHebrewText, std::nullopt, A::Natural)));
    EXPECT(drawnRight(inkStartsAt(kHebrewText, std::nullopt, std::nullopt)));
    // And from the prop when there is one.
    EXPECT(drawnRight(inkStartsAt("hello world", D::RightToLeft, A::Natural)));
    EXPECT(drawnRight(inkStartsAt("hello world", D::RightToLeft, std::nullopt)));
  }
}

#if BASALT_RN_MINOR >= 87
TEST(appkit_text_align_start_and_end_are_the_edges_the_line_runs_between) {
  @autoreleasepool {
    using A = facebook::react::TextAlignment;
    using D = facebook::react::WritingDirection;

    EXPECT(drawnLeft(inkStartsAt("hello world", std::nullopt, A::Start)));
    EXPECT(drawnRight(inkStartsAt("hello world", std::nullopt, A::End)));

    // The entry this closes: `end` is the left edge in a right-to-left
    // paragraph, however the direction was decided.
    EXPECT(drawnRight(inkStartsAt(kHebrewText, std::nullopt, A::Start)));
    EXPECT(drawnLeft(inkStartsAt(kHebrewText, std::nullopt, A::End)));
    EXPECT(drawnRight(inkStartsAt(kHebrewText, D::RightToLeft, A::Start)));
    EXPECT(drawnLeft(inkStartsAt(kHebrewText, D::RightToLeft, A::End)));
    EXPECT(drawnRight(inkStartsAt("hello world", D::RightToLeft, A::Start)));
    EXPECT(drawnLeft(inkStartsAt("hello world", D::RightToLeft, A::End)));
  }
}
#endif
