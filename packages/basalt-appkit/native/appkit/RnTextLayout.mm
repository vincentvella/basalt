#import "RnTextLayout.h"

#include "TextHighlight.h"
#include "TextVerticalAlign.h"

#include <cmath>

@implementation RnTextLayout {
  CTFramesetterRef _framesetter;
}

+ (instancetype)layoutWithAttributedString:(NSAttributedString *)attributedString
                      maximumNumberOfLines:(NSInteger)maximumNumberOfLines
                            truncationType:(CTLineTruncationType)truncationType
                                 truncates:(BOOL)truncates {
  RnTextLayout *layout = [[RnTextLayout alloc] init];
  layout->_attributedString = attributedString;
  layout->_maximumNumberOfLines = maximumNumberOfLines;
  layout->_truncationType = truncationType;
  layout->_truncates = truncates;
  layout->_framesetter =
      CTFramesetterCreateWithAttributedString((__bridge CFAttributedStringRef)attributedString);
  return layout;
}

- (void)dealloc {
  if (_framesetter != nullptr) {
    CFRelease(_framesetter);
  }
}

// The lines this paragraph breaks into at `maxWidth`, already limited to
// maximumNumberOfLines and with the last one truncated if it had to be.
//
// Core Text has no "maximum number of lines": a framesetter breaks as many
// lines as the text needs and a frame holds as many as fit in its path. So the
// limit is applied here, by asking for an unbounded frame and keeping the first
// N -- which is also the only way to put the ellipsis in the right place, since
// the line that gets truncated is the last one *kept*, not the last one there
// is.
- (NSArray *)linesForWidth:(CGFloat)maxWidth outLines:(NSMutableArray *)collected {
  const CGFloat width = maxWidth < 0 ? CGFLOAT_MAX : maxWidth;

  CGMutablePathRef path = CGPathCreateMutable();
  CGPathAddRect(path, nullptr, CGRectMake(0, 0, width, CGFLOAT_MAX));
  CTFrameRef frame = CTFramesetterCreateFrame(_framesetter, CFRangeMake(0, 0), path, nullptr);
  CGPathRelease(path);
  if (frame == nullptr) {
    return collected;
  }

  NSArray *lines = (__bridge NSArray *)CTFrameGetLines(frame);
  const NSInteger limit =
      _maximumNumberOfLines > 0 ? MIN(_maximumNumberOfLines, (NSInteger)lines.count)
                                : (NSInteger)lines.count;

  for (NSInteger i = 0; i < limit; i++) {
    CTLineRef line = (__bridge CTLineRef)lines[(NSUInteger)i];
    const BOOL isLastKept = (i == limit - 1);
    const BOOL droppedSomething = limit < (NSInteger)lines.count;

    if (_truncates && isLastKept && droppedSomething && width != CGFLOAT_MAX) {
      // The truncated line has to be built from everything that would have
      // followed, not from this line alone -- otherwise the ellipsis lands
      // after text that already fit and nothing looks omitted.
      const CFRange lineRange = CTLineGetStringRange(line);
      const CFIndex from = lineRange.location;
      NSAttributedString *rest =
          [_attributedString attributedSubstringFromRange:NSMakeRange(
              (NSUInteger)from, _attributedString.length - (NSUInteger)from)];

      // The token inherits the attributes of the text it replaces, so an
      // ellipsis in 40pt bold does not come out 14pt regular.
      NSDictionary *attributes = _attributedString.length > 0
          ? [_attributedString attributesAtIndex:(NSUInteger)MAX((CFIndex)0, from) effectiveRange:nullptr]
          : @{};
      NSAttributedString *ellipsis = [[NSAttributedString alloc] initWithString:@"…"
                                                                    attributes:attributes];
      CTLineRef token = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)ellipsis);
      CTLineRef full = CTLineCreateWithAttributedString((__bridge CFAttributedStringRef)rest);
      CTLineRef truncated = CTLineCreateTruncatedLine(full, width, _truncationType, token);
      CFRelease(token);
      CFRelease(full);
      if (truncated != nullptr) {
        [collected addObject:(__bridge_transfer id)truncated];
        continue;
      }
    }

    [collected addObject:(__bridge id)line];
  }

  CFRelease(frame);
  return collected;
}

// Line metrics, summed. Not CTFramesetterSuggestFrameSizeWithConstraints: that
// measures the whole text and cannot be told about a line limit, so a
// numberOfLines={2} paragraph would be laid out two lines tall and measured
// twelve.
- (CGSize)sizeForWidth:(CGFloat)maxWidth {
  NSMutableArray *lines = [NSMutableArray array];
  [self linesForWidth:maxWidth outLines:lines];

  CGFloat width = 0;
  CGFloat height = 0;
  for (id item in lines) {
    CTLineRef line = (__bridge CTLineRef)item;
    CGFloat ascent = 0, descent = 0, leading = 0;
    const double lineWidth = CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    width = MAX(width, (CGFloat)lineWidth);
    height += std::ceil(ascent + descent + leading);
  }
  return CGSizeMake(std::ceil(width), height);
}

- (CGRect)frameForCharacterIndex:(NSUInteger)index width:(CGFloat)maxWidth {
  NSMutableArray *lines = [NSMutableArray array];
  [self linesForWidth:maxWidth outLines:lines];

  // Walked the same way `sizeForWidth:` sums it, so a view's y agrees with the
  // paragraph height the same layout reported. Duplicating the accumulation
  // rather than sharing it would be the way these two drift apart.
  CGFloat top = 0;
  for (id item in lines) {
    CTLineRef line = (__bridge CTLineRef)item;
    CGFloat ascent = 0, descent = 0, leading = 0;
    (void)CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    const CGFloat lineHeight = std::ceil(ascent + descent + leading);

    const CFRange range = CTLineGetStringRange(line);
    const auto start = static_cast<NSUInteger>(range.location);
    const auto length = static_cast<NSUInteger>(range.length);
    if (index >= start && index < start + length) {
      const CGFloat x = CTLineGetOffsetForStringIndex(line, static_cast<CFIndex>(index), nullptr);

      // The run's own ascent and descent, not the line's: a short view on a line
      // of tall text is its own height, and asking the line would give it the
      // tallest thing on it. Found by the run whose range contains the index.
      CGFloat runAscent = ascent;
      CGFloat runDescent = descent;
      CFArrayRef runs = CTLineGetGlyphRuns(line);
      for (CFIndex r = 0; r < CFArrayGetCount(runs); r++) {
        CTRunRef run = static_cast<CTRunRef>(CFArrayGetValueAtIndex(runs, r));
        const CFRange runRange = CTRunGetStringRange(run);
        const auto runStart = static_cast<NSUInteger>(runRange.location);
        const auto runLength = static_cast<NSUInteger>(runRange.length);
        if (index >= runStart && index < runStart + runLength) {
          CTRunGetTypographicBounds(run, CFRangeMake(0, 0), &runAscent, &runDescent, nullptr);
          break;
        }
      }

      // Top-left, from a baseline-anchored box: the baseline sits `ascent` below
      // the line's top, and the run rises `runAscent` above the baseline.
      return CGRectMake(x,
                        top + (ascent - runAscent),
                        0,
                        runAscent + runDescent);
    }

    top += lineHeight;
  }

  // Past the end, or dropped by a truncation.
  return CGRectNull;
}

// How far along the line the text sits: 0 left, 0.5 centred, 1 right.
//
// Taken from the first fragment, because React Native resolves alignment onto
// every fragment from the <Text> that owns them -- the Pango side reads it the
// same way. Core Text puts alignment in the *frame*, and this draws lines one
// at a time so it can honour numberOfLines, so the frame's own alignment never
// applies and the offset has to be computed here. Without this every paragraph
// draws flush left and `textAlign` silently does nothing.
- (CGFloat)flushFactor {
  if (_attributedString.length == 0) {
    return 0;
  }
  NSParagraphStyle *style = [_attributedString attribute:NSParagraphStyleAttributeName
                                                 atIndex:0
                                          effectiveRange:nullptr];
  switch (style.alignment) {
    case NSTextAlignmentCenter:
      return 0.5;
    case NSTextAlignmentRight:
      return 1.0;
    case NSTextAlignmentLeft:
      // Explicitly left, which is not the same as natural: a right-to-left
      // paragraph with `textAlign: 'left'` is flush left, and this used to fall
      // through to the natural branch below and come out flush right.
      return 0;
    default:
      break;
  }

  // What is left is `natural` and `justified`, and both mean the edge the
  // paragraph *starts* from -- the relative alignments, `start` and `end`, were
  // resolved into left or right before they got here; see
  // core/TextAlignments.h.
  //
  // Which edge that is needs the paragraph's direction, and the style's own is
  // only half of it: `NSWritingDirectionNatural` means "ask the text", which a
  // frame would resolve and this does not use. `rightToLeft` is that answer,
  // from core/TextDirection.h. Measured on both hosts and wrong on both at
  // first: Pango flips a natural alignment only while it is deciding the
  // direction itself, and Core Text only inside a frame. The two fixes are in
  // different places and are the same decision. See PangoTextLayout.cpp.
  if (_rightToLeft || style.baseWritingDirection == NSWritingDirectionRightToLeft) {
    return 1.0;
  }
  return 0;
}

// `textAlignVertical`: where the paragraph sits in a box taller than it is. The
// height the text needs is the one `sizeForWidth:` sums, walked the same way
// here, so this asks the lines rather than measuring again.
//
// Its own method because the hit test needs the same number as the draw: a
// paragraph sitting at the bottom of its box is hit-tested there too.
- (CGFloat)verticalOffsetForSize:(CGSize)size lines:(NSArray *)lines {
  if (self.verticalFlush <= 0) {
    return 0;
  }
  CGFloat textHeight = 0;
  for (id item in lines) {
    CTLineRef line = (__bridge CTLineRef)item;
    CGFloat ascent = 0, descent = 0, leading = 0;
    (void)CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    textHeight += std::ceil(ascent + descent + leading);
  }
  return (CGFloat)basalt::textVerticalOffset(size.height, textHeight, self.verticalFlush);
}

- (NSUInteger)characterIndexAtPoint:(CGPoint)point size:(CGSize)size {
  NSMutableArray *lines = [NSMutableArray array];
  [self linesForWidth:size.width outLines:lines];
  if (lines.count == 0) {
    return 0;
  }

  // Which line the y falls in, walked the same way `sizeForWidth:` sums the
  // heights so that a hit test and a measurement agree about where a line is.
  // A point above the first line or below the last answers that line, which is
  // what a drag off the top or the bottom of a paragraph means.
  CGFloat top = [self verticalOffsetForSize:size lines:lines];
  CTLineRef found = (__bridge CTLineRef)lines[0];
  for (id item in lines) {
    CTLineRef line = (__bridge CTLineRef)item;
    CGFloat ascent = 0, descent = 0, leading = 0;
    (void)CTLineGetTypographicBounds(line, &ascent, &descent, &leading);
    const CGFloat height = std::ceil(ascent + descent + leading);
    found = line;
    if (point.y < top + height) {
      break;
    }
    top += height;
  }

  // The x, in the line's own coordinates: a right-aligned or centred paragraph
  // draws its lines offset, and the index at a point has to use the same
  // offset the glyphs were drawn with.
  const CGFloat flush = [self flushFactor];
  const CGFloat lineX =
      flush == 0 ? 0 : (CGFloat)CTLineGetPenOffsetForFlush(found, flush, size.width);
  const CFIndex index =
      CTLineGetStringIndexForPosition(found, CGPointMake(point.x - lineX, 0));
  return index == kCFNotFound ? 0 : (NSUInteger)index;
}

- (nullable NSString *)selectedText {
  if (_selection.length == 0 || _attributedString.length == 0) {
    return nil;
  }
  // Clamped against the string rather than trusted: a paragraph can be rebuilt
  // between a selection and a copy, and the old offsets would read past the end
  // of a shorter one.
  const NSUInteger length = _attributedString.length;
  const NSUInteger start = MIN(_selection.location, length);
  const NSUInteger end = MIN(start + _selection.length, length);
  if (end <= start) {
    return nil;
  }
  return [_attributedString.string substringWithRange:NSMakeRange(start, end - start)];
}

- (void)drawInContext:(CGContextRef)context size:(CGSize)size {
  NSMutableArray *lines = [NSMutableArray array];
  [self linesForWidth:size.width outLines:lines];

  const CGFloat flush = [self flushFactor];

  CGContextSaveGState(context);
  // Core Text draws with the origin at the baseline and y increasing upward.
  // The view above is flipped, so the context arrives y-down; this puts it back
  // for the duration of the draw and leaves it as it was found.
  CGContextTranslateCTM(context, 0, size.height);
  CGContextScaleCTM(context, 1, -1);
  CGContextSetTextMatrix(context, CGAffineTransformIdentity);

  // The text shadow, on the context: Core Text ignores an NSShadow attribute,
  // and one shadow for the paragraph is what core/TextShadows.h resolves anyway.
  //
  // The offset's y is negated because the context has just been flipped back to
  // Core Text's orientation, where y grows upward: React Native means a positive
  // offset as *down* the screen, which is what the GTK side gets for free.
  if (self.shadowColor != nil) {
    CGContextSetShadowWithColor(context,
                                CGSizeMake(self.shadowOffset.width, -self.shadowOffset.height),
                                self.shadowStandardDeviation,
                                self.shadowColor.CGColor);
  }

  // Core Text's y grows upward and the context has just been flipped back into
  // that, so moving the paragraph *down* the box means starting lower.
  CGFloat y = size.height - [self verticalOffsetForSize:size lines:lines];
  for (id item in lines) {
    CTLineRef line = (__bridge CTLineRef)item;
    CGFloat ascent = 0, descent = 0, leading = 0;
    CTLineGetTypographicBounds(line, &ascent, &descent, &leading);

    y -= std::ceil(ascent + descent + leading);
    const CGFloat x = flush == 0 ? 0 : (CGFloat)CTLineGetPenOffsetForFlush(line, flush, size.width);

    // The selection, under this line's glyphs and before them: a wash rather
    // than a solid fill, because nothing recolours the text and it has to stay
    // legible through it. See core/TextHighlight.h for why the colour is this
    // project's rather than the system's.
    if (_selection.length > 0) {
      const CFRange lineRange = CTLineGetStringRange(line);
      const NSUInteger lineStart = (NSUInteger)lineRange.location;
      const NSUInteger lineEnd = lineStart + (NSUInteger)lineRange.length;
      const NSUInteger from = MAX(_selection.location, lineStart);
      const NSUInteger to = MIN(_selection.location + _selection.length, lineEnd);
      if (to > from) {
        // Two offsets along the line, which is where a bidirectional line needs
        // more than one rectangle -- a selection contiguous in the string is not
        // contiguous on the line. Taking the span between the two offsets is one
        // rectangle and is what iOS's own `UITextView` draws for the common
        // case; the mixed-direction case is recorded in backlog/text.md.
        const CGFloat startX = CTLineGetOffsetForStringIndex(line, (CFIndex)from, nullptr);
        const CGFloat endX = CTLineGetOffsetForStringIndex(line, (CFIndex)to, nullptr);
        const CGRect wash = CGRectMake(x + MIN(startX, endX),
                                       y,
                                       std::abs(endX - startX),
                                       std::ceil(ascent + descent + leading));
        CGContextSaveGState(context);
        CGContextSetRGBFillColor(context,
                                 basalt::kTextSelectionRed,
                                 basalt::kTextSelectionGreen,
                                 basalt::kTextSelectionBlue,
                                 basalt::kTextSelectionAlpha);
        CGContextFillRect(context, wash);
        CGContextRestoreGState(context);
      }
    }

    CGContextSetTextPosition(context, x, y + std::ceil(descent + leading));
    CTLineDraw(line, context);
  }

  CGContextRestoreGState(context);
}

@end
