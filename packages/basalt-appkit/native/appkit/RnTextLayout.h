// A laid-out paragraph, and nothing that knows what React Native is.
//
// Deliberately separate from CoreTextLayout.h, which builds one of these out of
// an AttributedString. The view layer draws paragraphs and must not gain a
// React Native dependency to do it -- basalt_appkit_view builds and is tested
// on a Mac with nothing else installed, and that is worth keeping.

#pragma once

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>

@interface RnTextLayout : NSObject

@property(nonatomic, readonly) NSAttributedString *attributedString;
// 0 means no limit, matching ParagraphAttributes::maximumNumberOfLines.
@property(nonatomic, readonly) NSInteger maximumNumberOfLines;
@property(nonatomic, readonly) CTLineTruncationType truncationType;
@property(nonatomic, readonly) BOOL truncates;

+ (instancetype)layoutWithAttributedString:(NSAttributedString *)attributedString
                      maximumNumberOfLines:(NSInteger)maximumNumberOfLines
                            truncationType:(CTLineTruncationType)truncationType
                                 truncates:(BOOL)truncates;

// The size this paragraph needs at `maxWidth`. Pass a negative width for
// unconstrained. In React Native's logical points, like everything else here.
//
// Width is not baked in: a PangoLayout has to be built against a width and
// rebuilt when it changes, and a Core Text framesetter does not. One of the few
// places the two platforms differ in shape rather than in spelling.
- (CGSize)sizeForWidth:(CGFloat)maxWidth;

// Draws into `context`, top-left origin, in a box `size` wide and tall.
- (void)drawInContext:(CGContextRef)context size:(CGSize)size;

@end
#endif
