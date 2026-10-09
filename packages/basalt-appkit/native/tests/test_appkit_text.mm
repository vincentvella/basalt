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
#import "RnTextLayout.h"

#include <algorithm>
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
