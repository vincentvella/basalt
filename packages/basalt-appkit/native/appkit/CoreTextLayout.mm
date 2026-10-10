#import "CoreTextLayout.h"

#include "TextAlignments.h"
#include "TextDirection.h"
#include "WritingDirections.h"

#include "FontRegistry.h"
#include "FontScaling.h"
#include "FontVariants.h"
#include "TextColors.h"
#include "TextDecorations.h"

#include <react/renderer/graphics/Color.h>

#include <cmath>
#include <string>


// ---------------------------------------------------------------------------
// AttributedString -> NSAttributedString
// ---------------------------------------------------------------------------

namespace basalt {

using facebook::react::AttributedString;
using facebook::react::EllipsizeMode;
using facebook::react::FontStyle;
using facebook::react::FontWeight;
using facebook::react::ParagraphAttributes;
using facebook::react::TextAlignment;
using facebook::react::TextAttributes;
using facebook::react::TextDecorationLineType;

namespace {

// React Native's default when a <Text> sets no fontSize.
constexpr float kDefaultFontSize = 14.0F;

bool isSet(facebook::react::Float value) {
  return !std::isnan(value);
}

// CSS numeric weights to Core Text's -1..1 scale, using Apple's own constants
// for the named steps. A straight linear mapping would put "bold" somewhere
// between semibold and heavy and make every bold face on the system slightly
// wrong.
CGFloat toCoreTextWeight(FontWeight weight) {
  switch (static_cast<int>(weight)) {
    case 100: return NSFontWeightUltraLight;
    case 200: return NSFontWeightThin;
    case 300: return NSFontWeightLight;
    case 400: return NSFontWeightRegular;
    case 500: return NSFontWeightMedium;
    case 600: return NSFontWeightSemibold;
    case 700: return NSFontWeightBold;
    case 800: return NSFontWeightHeavy;
    case 900: return NSFontWeightBlack;
    default: break;
  }
  return NSFontWeightRegular;
}

// An edge, spelled as AppKit spells it. The relative alignments are gone by
// here -- core/TextAlignments.h resolved them against the paragraph's direction
// -- so what arrives is one of four.
//
// `NSTextAlignmentNatural` is deliberately not in the answers: it is the style
// default and stays in place for a paragraph that asked for `natural` or asked
// for nothing, which is what lets an `NSTextField` resolve a field's own
// direction. Folding `left` into it is what made `textAlign: 'left'` draw
// flush right in a right-to-left paragraph.
NSTextAlignment toTextAlignment(basalt::PhysicalTextAlignment alignment) {
  switch (alignment) {
    case basalt::PhysicalTextAlignment::Center:
      return NSTextAlignmentCenter;
    case basalt::PhysicalTextAlignment::Right:
      return NSTextAlignmentRight;
    case basalt::PhysicalTextAlignment::Justified:
      return NSTextAlignmentJustified;
    case basalt::PhysicalTextAlignment::Left:
      break;
  }
  return NSTextAlignmentLeft;
}

// A colour core resolved, in sRGB. Explicitly sRGB for the same reason
// RnAppKitView builds its background that way: a device colour space shifts
// every colour on a wide-gamut display, and text that does not match Linux is
// the bug this project is least able to afford.
NSColor *toColor(const basalt::TextColor &color) {
  return [NSColor colorWithSRGBRed:color.red
                             green:color.green
                              blue:color.blue
                             alpha:color.alpha];
}

// React Native's five decoration styles against Core Text's bitmask.
//
// A style and a pattern are OR'd into one value: `NSUnderlineStyleSingle` with
// `NSUnderlineStylePatternDot` is the dotted line CSS means. There is no wavy
// pattern -- the squiggle under a misspelling is drawn by the text system's
// spell checking and not by an underline style -- so `wavy` falls back to a
// single line here and is exact on GTK, which is the opposite way round from
// dotted and dashed. core/TextDecorations.h says why falling back beats
// refusing, and backlog/text.md records both gaps.
NSUnderlineStyle toUnderlineStyle(facebook::react::TextDecorationStyle style) {
  switch (style) {
    case facebook::react::TextDecorationStyle::Double:
      return NSUnderlineStyleDouble;
    case facebook::react::TextDecorationStyle::Dotted:
      return static_cast<NSUnderlineStyle>(NSUnderlineStyleSingle | NSUnderlineStylePatternDot);
    case facebook::react::TextDecorationStyle::Dashed:
      return static_cast<NSUnderlineStyle>(NSUnderlineStyleSingle | NSUnderlineStylePatternDash);
    case facebook::react::TextDecorationStyle::Solid:
    case facebook::react::TextDecorationStyle::Wavy:
      return NSUnderlineStyleSingle;
  }
  return NSUnderlineStyleSingle;
}

NSFont *fontFor(const TextAttributes &textAttributes, basalt::FontFit fit = {}) {
  // Through core/FontScaling.h, which is where `allowFontScaling` and
  // `maxFontSizeMultiplier` are honoured. macOS reports no text scale to honour
  // -- see appKitTextScale() -- so on this host those two props can only clamp
  // a multiplier something else set, and the test that proves they are read
  // supplies one with BASALT_TEST_FONT_SCALE.
  // `adjustsFontSizeToFit` last, over the size those props resolved to:
  // shrinking to fit is about the size the text would otherwise have been,
  // including the desktop's own scale. A paragraph that never asked carries a
  // ratio of 1 and this changes nothing.
  const float size = static_cast<float>(fit.apply(
      basalt::effectiveFontSize(textAttributes, kDefaultFontSize, basalt::systemFontScale())));

  const CGFloat weight = textAttributes.fontWeight ? toCoreTextWeight(*textAttributes.fontWeight)
                                                   : NSFontWeightRegular;
  const bool italic = textAttributes.fontStyle &&
      (*textAttributes.fontStyle == FontStyle::Italic ||
       *textAttributes.fontStyle == FontStyle::Oblique);

  NSFont *font = nil;
  if (textAttributes.fontFamily.empty()) {
    // The system font, which is what "no fontFamily" means on a Mac -- and
    // asking for it by name would get the wrong thing, since San Francisco is
    // not available under its own family name.
    font = [NSFont systemFontOfSize:size weight:weight];
  } else {
    // Through the runtime registry: a font an app loaded is known to the app by
    // a name it chose and to Core Text by the name inside the file.
    // resolveFontFamily returns the argument unchanged for system families.
    const std::string family = resolveFontFamily(textAttributes.fontFamily);
    NSString *familyName = [NSString stringWithUTF8String:family.c_str()];

    NSFontDescriptor *descriptor = [NSFontDescriptor fontDescriptorWithFontAttributes:@{
      NSFontFamilyAttribute : familyName,
      NSFontTraitsAttribute : @{NSFontWeightTrait : @(weight)},
    }];
    font = [NSFont fontWithDescriptor:descriptor size:size];
    if (font == nil) {
      // A family the system does not have. Falling back to the system font
      // rather than to nil, because a nil font makes Core Text drop the run
      // entirely and the text silently disappears.
      font = [NSFont systemFontOfSize:size weight:weight];
    }
  }

  if (italic) {
    NSFontDescriptor *italicised =
        [font.fontDescriptor fontDescriptorWithSymbolicTraits:NSFontDescriptorTraitItalic];
    NSFont *slanted = [NSFont fontWithDescriptor:italicised size:size];
    if (slanted != nil) {
      font = slanted;
    }
  }

  // `fontVariant`, which on this platform is a font and not an attribute: Core
  // Text asks for features on the descriptor, so the font has to be rebuilt.
  // Last, so the features are added to whatever family and slant was settled
  // on above.
  NSArray *const features = fontFeaturesFor(textAttributes);
  if (features.count > 0) {
    NSFontDescriptor *featured = [font.fontDescriptor fontDescriptorByAddingAttributes:@{
      NSFontFeatureSettingsAttribute : features,
    }];
    NSFont *varied = [NSFont fontWithDescriptor:featured size:size];
    if (varied != nil) {
      font = varied;
    }
  }

  return font;
}

NSParagraphStyle *paragraphStyleFor(const TextAttributes &textAttributes, bool rightToLeft) {
  NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];

  // `natural` is left alone rather than resolved here: it is this style's own
  // default, and leaving it is what lets a frame -- or an NSTextField, which
  // has its own text and its own direction -- resolve it. RnTextLayout resolves
  // it for the paragraph path, where no frame is used; see its `rightToLeft`.
  if (textAttributes.alignment && *textAttributes.alignment != TextAlignment::Natural) {
    style.alignment =
        toTextAlignment(basalt::physicalTextAlignment(*textAttributes.alignment, rightToLeft));
  }

  // `baseWritingDirection`, which is the same call upstream's iOS half makes in
  // `RCTAttributedTextUtils.mm`. `Natural` is the paragraph style's own default
  // and means the Unicode bidi algorithm decides from the first strong
  // character, so it is set explicitly rather than skipped: a nested <Text>
  // inherits this style, and leaving it unset would let the enclosing
  // paragraph's direction stand where the app asked for the natural one.
  if (textAttributes.baseWritingDirection) {
    switch (*textAttributes.baseWritingDirection) {
      case facebook::react::WritingDirection::Natural:
        style.baseWritingDirection = NSWritingDirectionNatural;
        break;
      case facebook::react::WritingDirection::LeftToRight:
        style.baseWritingDirection = NSWritingDirectionLeftToRight;
        break;
      case facebook::react::WritingDirection::RightToLeft:
        style.baseWritingDirection = NSWritingDirectionRightToLeft;
        break;
    }
  }

  // React Native's lineHeight is the total line box height, and setting both
  // bounds to it is how that is said in AppKit. Leaving one of them out gives a
  // minimum or a maximum, which is a different thing and only shows up on text
  // whose natural height is on the other side of the value.
  if (isSet(textAttributes.lineHeight)) {
    const CGFloat lineHeight = static_cast<CGFloat>(textAttributes.lineHeight);
    style.minimumLineHeight = lineHeight;
    style.maximumLineHeight = lineHeight;
  }

  // Wrapping, always. The line *limit* is applied in RnTextLayout, not here:
  // a truncating line break mode would make Core Text ellipsize each line it
  // lays out rather than the last one kept.
  style.lineBreakMode = NSLineBreakByWordWrapping;

  return style;
}

} // namespace

float appKitTextScale() { return 1.0F; }

// `fontVariant` in Core Text's vocabulary, which is not OpenType's.
//
// Pango and DirectWrite take the four-character tags core/FontVariants.h hands
// out; Core Text takes Apple's older AAT pair, a feature type and a selector
// inside it. This is upstream's own table, from `RCTFontFeatures` in
// RCTFontUtils.mm, which is where to look if a variant ever renders differently
// from iOS.
//
// The twenty stylistic alternates are a formula rather than twenty entries: the
// on-selectors run 2, 4, 6 and so on, which the suite asserts against the SDK's
// own `kStylisticAltOneOnSelector` and `kStylisticAltTwentyOnSelector` rather
// than taking this comment's word for it.
NSArray *fontFeaturesFor(const TextAttributes &textAttributes) {
  NSMutableArray *features = [NSMutableArray array];
  for (const auto variant : basalt::fontVariants(textAttributes)) {
    int type = 0;
    int selector = 0;
    switch (variant) {
      case facebook::react::FontVariant::SmallCaps:
        type = kLowerCaseType;
        selector = kLowerCaseSmallCapsSelector;
        break;
      case facebook::react::FontVariant::OldstyleNums:
        type = kNumberCaseType;
        selector = kLowerCaseNumbersSelector;
        break;
      case facebook::react::FontVariant::LiningNums:
        type = kNumberCaseType;
        selector = kUpperCaseNumbersSelector;
        break;
      case facebook::react::FontVariant::TabularNums:
        type = kNumberSpacingType;
        selector = kMonospacedNumbersSelector;
        break;
      case facebook::react::FontVariant::ProportionalNums:
        type = kNumberSpacingType;
        selector = kProportionalNumbersSelector;
        break;
      case facebook::react::FontVariant::Default:
        continue;
      default: {
        // A stylistic alternate, which the tag says which of: `ss07` is the
        // seventh, and its on-selector is twice that.
        const char *const tag = basalt::openTypeTag(variant);
        if (tag == nullptr || tag[0] != 's' || tag[1] != 's') {
          continue;
        }
        const int number = (tag[2] - '0') * 10 + (tag[3] - '0');
        type = kStylisticAlternativesType;
        selector = number * 2;
        break;
      }
    }
    [features addObject:@{
      NSFontFeatureTypeIdentifierKey : @(type),
      NSFontFeatureSelectorIdentifierKey : @(selector),
    }];
  }
  return features;
}

NSDictionary<NSAttributedStringKey, id> *buildTextAttributes(const TextAttributes &textAttributes,
                                                            basalt::FontFit fit,
                                                            bool rightToLeft) {
  NSMutableDictionary<NSAttributedStringKey, id> *attributes = [NSMutableDictionary dictionary];

  attributes[NSFontAttributeName] = fontFor(textAttributes, fit);
  attributes[NSParagraphStyleAttributeName] = paragraphStyleFor(textAttributes, rightToLeft);

  // The foreground, with `opacity` multiplied in, and React Native's default of
  // opaque black rather than the system label colour -- which is white in dark
  // mode and would make default text invisible on a light background. Both
  // rules are core/TextColors.h's now, so GTK cannot drift from them.
  attributes[NSForegroundColorAttributeName] =
      toColor(basalt::textForegroundColor(textAttributes));

  if (const auto background = basalt::textBackgroundColor(textAttributes)) {
    attributes[NSBackgroundColorAttributeName] = toColor(*background);
  }

  if (isSet(textAttributes.letterSpacing)) {
    attributes[NSKernAttributeName] = @(static_cast<CGFloat>(textAttributes.letterSpacing));
  }

  // Which lines, in which colour, in which style: resolved in
  // core/TextDecorations.h so both hosts agree about what was asked for, and
  // mapped here to Core Text's one bitmask.
  if (const auto decoration = basalt::textDecoration(textAttributes)) {
    const NSUnderlineStyle style = toUnderlineStyle(decoration->style);
    if (decoration->underline) {
      attributes[NSUnderlineStyleAttributeName] = @(style);
    }
    if (decoration->strikethrough) {
      // The same bitmask as an underline, which is one thing AppKit has over
      // Pango here: a dotted strikethrough is a dotted strikethrough.
      attributes[NSStrikethroughStyleAttributeName] = @(style);
    }
    if (decoration->hasColor) {
      NSColor *const color = toColor(basalt::TextColor{
          decoration->red, decoration->green, decoration->blue, decoration->alpha});
      if (decoration->underline) {
        attributes[NSUnderlineColorAttributeName] = color;
      }
      if (decoration->strikethrough) {
        attributes[NSStrikethroughColorAttributeName] = color;
      }
    }
  }

  return attributes;
}

// The callbacks a run delegate answers with. CoreText asks for three numbers and
// the box is anchored on the baseline, so the whole height is ascent and the
// descent is zero, which puts the view sitting on the line rather than straddling
// it. That matches the shape attribute on GTK, where the rect runs from -height
// to 0.
//
// The context is a heap-allocated Size owned by the delegate: `dealloc` frees it,
// and CoreText guarantees it is called once the delegate goes.
CGFloat attachmentAscent(void *context) {
  return static_cast<CGFloat>(static_cast<facebook::react::Size *>(context)->height);
}

CGFloat attachmentDescent(void * /*context*/) {
  return 0;
}

CGFloat attachmentWidth(void *context) {
  return static_cast<CGFloat>(static_cast<facebook::react::Size *>(context)->width);
}

void attachmentDealloc(void *context) {
  delete static_cast<facebook::react::Size *>(context);
}

CTRunDelegateRef makeAttachmentDelegate(const facebook::react::Size &size) {
  CTRunDelegateCallbacks callbacks = {};
  callbacks.version = kCTRunDelegateCurrentVersion;
  callbacks.dealloc = attachmentDealloc;
  callbacks.getAscent = attachmentAscent;
  callbacks.getDescent = attachmentDescent;
  callbacks.getWidth = attachmentWidth;
  return CTRunDelegateCreate(&callbacks, new facebook::react::Size{size});
}

// `textTransform`, applied before anything measures or draws the text.
//
// NSString's case mapping rather than a shared one, for the reason the GTK host
// uses GLib's: Unicode case mapping is the toolkit's, and a byte-wise transform
// would leave ß and every accented letter alone while looking right in English.
// The two hosts are asserted against the same cases.
//
// `capitalize` is React Native's own rule, from RCTAttributedTextUtils.mm: split
// on single spaces, and a word whose first character is not a digit is
// capitalised -- which lowercases the rest of it, so "iOS" becomes "Ios". That
// is surprising and is what the other platforms do.
NSString *transformedFragmentText(const AttributedString::Fragment &fragment, NSString *text) {
  const auto transform = fragment.textAttributes.textTransform;
  if (!transform.has_value()) {
    return text;
  }
  switch (*transform) {
    case facebook::react::TextTransform::Uppercase:
      return text.uppercaseString;
    case facebook::react::TextTransform::Lowercase:
      return text.lowercaseString;
    case facebook::react::TextTransform::Capitalize: {
      NSMutableArray<NSString *> *words = [NSMutableArray array];
      for (NSString *word in [text componentsSeparatedByString:@" "]) {
        if (word.length == 0) {
          [words addObject:word];
          continue;
        }
        NSRange first = [word rangeOfComposedCharacterSequenceAtIndex:0];
        NSString *lead = [word substringWithRange:first];
        const BOOL isDigit = [lead rangeOfCharacterFromSet:NSCharacterSet.decimalDigitCharacterSet]
                                 .location != NSNotFound;
        [words addObject:isDigit ? word.lowercaseString : word.capitalizedString];
      }
      return [words componentsJoinedByString:@" "];
    }
    case facebook::react::TextTransform::None:
    case facebook::react::TextTransform::Unset:
      break;
  }
  return text;
}

basalt::FontFit textFitScale(const AttributedString &attributedString,
                             const ParagraphAttributes &paragraphAttributes,
                             float maxWidth,
                             float maxHeight) {
  const basalt::FontFit limits = basalt::fontFitLimits(paragraphAttributes);
  if (!paragraphAttributes.adjustsFontSizeToFit) {
    return limits;
  }

  // Measured against the paragraph *without* its line limit, which is how this
  // prop and `numberOfLines` work together. iOS tests whether the text was
  // truncated; asking what the untruncated paragraph needs answers the same
  // question with one layout instead of two, and is the reason a one-line
  // button label shrinks rather than ellipsising.
  ParagraphAttributes untruncated = paragraphAttributes;
  untruncated.maximumNumberOfLines = 0;

  return basalt::fontFitToBox(
      limits,
      [&](double ratio) {
        basalt::FontFit probe = limits;
        probe.ratio = ratio;
        RnTextLayout *layout = buildTextLayout(attributedString, untruncated, probe);
        const CGSize size = [layout sizeForWidth:maxWidth];
        return basalt::FontFitBox{.width = size.width, .height = size.height};
      },
      basalt::FontFitBox{.width = maxWidth, .height = maxHeight});
}

RnTextLayout *buildTextLayout(const AttributedString &attributedString,
                              const ParagraphAttributes &paragraphAttributes,
                              basalt::FontFit fit) {
  NSMutableAttributedString *string = [[NSMutableAttributedString alloc] init];

  // Which way the paragraph runs, once for the whole of it: the prop when the
  // app set one, and Unicode's rule P2 over the text when it said `natural` or
  // said nothing. Every relative alignment needs it, and Core Text will not
  // answer it here -- it resolves a direction inside a frame, and this layer
  // draws its own lines. See core/TextDirection.h.
  const bool rightToLeft = basalt::paragraphIsRightToLeft(
      basalt::writingDirection(attributedString), basalt::paragraphText(attributedString));

  for (const auto &fragment : attributedString.getFragments()) {
    if (fragment.string.empty()) {
      continue;
    }
    NSString *text = [NSString stringWithUTF8String:fragment.string.c_str()];
    if (text == nil) {
      continue;
    }
    text = transformedFragmentText(fragment, text);

    NSMutableDictionary *attributes =
        [buildTextAttributes(fragment.textAttributes, fit, rightToLeft) mutableCopy];

    // An inline `<View>`: one fragment holding U+FFFC, whose own size React
    // Native has already measured into the fragment. A run delegate is
    // CoreText's way to say "this character is this big", the counterpart of
    // Pango's shape attribute on the other host. Without it the character
    // reserves whatever the font gives a missing glyph and the view is reported
    // at zero. See backlog/text.md.
    if (fragment.isAttachment()) {
      const auto &size = fragment.parentShadowView.layoutMetrics.frame.size;
      if (size.width > 0 && size.height > 0) {
        if (CTRunDelegateRef delegate = makeAttachmentDelegate(size)) {
          attributes[(__bridge NSString *)kCTRunDelegateAttributeName] =
              (__bridge_transfer id)delegate;
        }
      }
    }

    [string appendAttributedString:[[NSAttributedString alloc] initWithString:text
                                                                  attributes:attributes]];
  }

  CTLineTruncationType truncation = kCTLineTruncationEnd;
  bool truncates = true;
  switch (paragraphAttributes.ellipsizeMode) {
    case EllipsizeMode::Head:
      truncation = kCTLineTruncationStart;
      break;
    case EllipsizeMode::Middle:
      truncation = kCTLineTruncationMiddle;
      break;
    case EllipsizeMode::Tail:
      truncation = kCTLineTruncationEnd;
      break;
    case EllipsizeMode::Clip:
      // Cut, with no ellipsis. The line limit still applies.
      truncates = false;
      break;
  }

  RnTextLayout *layout =
      [RnTextLayout layoutWithAttributedString:string
                         maximumNumberOfLines:paragraphAttributes.maximumNumberOfLines
                               truncationType:truncation
                                    truncates:truncates ? YES : NO];
  layout.rightToLeft = rightToLeft ? YES : NO;
  // `textAlignVertical`, which is where the paragraph sits in a box taller than
  // it is. Applied while drawing, the box being known only then.
  layout.verticalFlush =
      basalt::textVerticalFlushFactor(paragraphAttributes.textAlignVertical);
  return layout;
}

} // namespace basalt
