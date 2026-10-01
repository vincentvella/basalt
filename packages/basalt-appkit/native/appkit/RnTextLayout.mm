#import "RnTextLayout.h"

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
    default:
      return 0;
  }
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

  CGFloat y = size.height;
  for (id item in lines) {
    CTLineRef line = (__bridge CTLineRef)item;
    CGFloat ascent = 0, descent = 0, leading = 0;
    CTLineGetTypographicBounds(line, &ascent, &descent, &leading);

    y -= std::ceil(ascent + descent + leading);
    const CGFloat x = flush == 0 ? 0 : (CGFloat)CTLineGetPenOffsetForFlush(line, flush, size.width);
    CGContextSetTextPosition(context, x, y + std::ceil(descent + leading));
    CTLineDraw(line, context);
  }

  CGContextRestoreGState(context);
}

@end
