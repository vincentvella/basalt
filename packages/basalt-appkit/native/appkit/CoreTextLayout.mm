#import "CoreTextLayout.h"

#include "FontRegistry.h"

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

NSTextAlignment toTextAlignment(TextAlignment alignment) {
  switch (alignment) {
    case TextAlignment::Center:
      return NSTextAlignmentCenter;
    case TextAlignment::Right:
#if BASALT_RN_MINOR >= 87
    case TextAlignment::End:
#endif
      return NSTextAlignmentRight;
    case TextAlignment::Justified:
      return NSTextAlignmentJustified;
    case TextAlignment::Left:
#if BASALT_RN_MINOR >= 87
    case TextAlignment::Start:
#endif
    case TextAlignment::Natural:
      break;
  }
  return NSTextAlignmentNatural;
}

NSColor *toColor(const facebook::react::SharedColor &color) {
  const auto components = facebook::react::colorComponentsFromColor(color);
  // sRGB explicitly, for the same reason RnAppKitView builds its background
  // that way: a device colour space shifts every colour on a wide-gamut
  // display, and text that does not match Linux is the bug this project is
  // least able to afford.
  return [NSColor colorWithSRGBRed:components.red
                             green:components.green
                              blue:components.blue
                             alpha:components.alpha];
}

NSFont *fontFor(const TextAttributes &textAttributes) {
  float size = isSet(textAttributes.fontSize) ? static_cast<float>(textAttributes.fontSize)
                                              : kDefaultFontSize;
  if (isSet(textAttributes.fontSizeMultiplier) && textAttributes.fontSizeMultiplier > 0) {
    size *= static_cast<float>(textAttributes.fontSizeMultiplier);
  }

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

  return font;
}

NSParagraphStyle *paragraphStyleFor(const TextAttributes &textAttributes) {
  NSMutableParagraphStyle *style = [[NSMutableParagraphStyle alloc] init];

  if (textAttributes.alignment) {
    style.alignment = toTextAlignment(*textAttributes.alignment);
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

NSDictionary<NSAttributedStringKey, id> *buildTextAttributes(const TextAttributes &textAttributes) {
  NSMutableDictionary<NSAttributedStringKey, id> *attributes = [NSMutableDictionary dictionary];

  attributes[NSFontAttributeName] = fontFor(textAttributes);
  attributes[NSParagraphStyleAttributeName] = paragraphStyleFor(textAttributes);

  if (textAttributes.foregroundColor) {
    attributes[NSForegroundColorAttributeName] = toColor(textAttributes.foregroundColor);
  } else {
    // React Native's default is opaque black, not the system label colour --
    // which is white in dark mode and would make default text invisible on a
    // light background, or vice versa. Matching React Native rather than the
    // platform is the right call here: the same app has to look the same on
    // Linux.
    attributes[NSForegroundColorAttributeName] = [NSColor colorWithSRGBRed:0 green:0 blue:0 alpha:1];
  }

  if (textAttributes.backgroundColor) {
    attributes[NSBackgroundColorAttributeName] = toColor(textAttributes.backgroundColor);
  }

  if (isSet(textAttributes.letterSpacing)) {
    attributes[NSKernAttributeName] = @(static_cast<CGFloat>(textAttributes.letterSpacing));
  }

  if (textAttributes.textDecorationLineType) {
    switch (*textAttributes.textDecorationLineType) {
      case TextDecorationLineType::Underline:
        attributes[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle);
        break;
      case TextDecorationLineType::Strikethrough:
        attributes[NSStrikethroughStyleAttributeName] = @(NSUnderlineStyleSingle);
        break;
      case TextDecorationLineType::UnderlineStrikethrough:
        attributes[NSUnderlineStyleAttributeName] = @(NSUnderlineStyleSingle);
        attributes[NSStrikethroughStyleAttributeName] = @(NSUnderlineStyleSingle);
        break;
      case TextDecorationLineType::None:
        break;
    }
  }

  return attributes;
}

RnTextLayout *buildTextLayout(const AttributedString &attributedString,
                              const ParagraphAttributes &paragraphAttributes) {
  NSMutableAttributedString *string = [[NSMutableAttributedString alloc] init];

  for (const auto &fragment : attributedString.getFragments()) {
    if (fragment.string.empty()) {
      continue;
    }
    NSString *text = [NSString stringWithUTF8String:fragment.string.c_str()];
    if (text == nil) {
      continue;
    }
    [string appendAttributedString:[[NSAttributedString alloc]
                                       initWithString:text
                                           attributes:buildTextAttributes(fragment.textAttributes)]];
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

  return [RnTextLayout layoutWithAttributedString:string
                             maximumNumberOfLines:paragraphAttributes.maximumNumberOfLines
                                   truncationType:truncation
                                        truncates:truncates ? YES : NO];
}

} // namespace basalt
