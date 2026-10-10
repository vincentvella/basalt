#include "DirectWriteLayout.h"

#include "TextDirection.h"
#include "WritingDirections.h"

#include "FontScaling.h"

#include "FontRegistry.h"
#include "FontVariants.h"
#include "TextColors.h"
#include "TextDecorations.h"
#include "TextShadows.h"

#include <windows.h>

#include <cwctype>
#include <optional>
#include <string>

#include <react/renderer/graphics/Color.h>

#include <cmath>

namespace basalt::win32 {
namespace {

using facebook::react::AttributedString;
using facebook::react::ParagraphAttributes;
using facebook::react::TextAttributes;

RnTextAlign toAlign(const TextAttributes &attributes) {
  if (!attributes.alignment.has_value()) {
    return RnTextAlign::Natural;
  }
  switch (*attributes.alignment) {
    case facebook::react::TextAlignment::Center:
      return RnTextAlign::Center;
    case facebook::react::TextAlignment::Right:
      return RnTextAlign::Right;
    case facebook::react::TextAlignment::Justified:
      return RnTextAlign::Justified;
    case facebook::react::TextAlignment::Left:
      return RnTextAlign::Left;
#if BASALT_RN_MINOR >= 87
    // React Native 0.87 added the writing-direction-relative spellings, and
    // this host keeps them relative rather than folding them into the physical
    // pair: DirectWrite has LEADING and TRAILING, so `start` and `end` cost
    // nothing to honour and follow the paragraph's direction on their own.
    // Left out of this switch entirely, `end` once fell through to
    // left-aligned, which clang said out loud in a warning nobody had
    // recompiled this file to see.
    //
    // This is why this host does not use `core/TextAlignments.h`, which the
    // other two do: that table turns a relative alignment into a physical edge,
    // and DirectWrite wants the relative one. What this host needed instead was
    // the *direction* resolved, which is the other half of the same entry --
    // `SetReadingDirection` is told and never asks the text, so a paragraph of
    // Hebrew was laid out left to right and every relative alignment in it
    // pointed at the wrong edge. See buildTextStyle.
    case facebook::react::TextAlignment::End:
      return RnTextAlign::End;
    case facebook::react::TextAlignment::Start:
      return RnTextAlign::Natural;
#endif
    case facebook::react::TextAlignment::Natural:
      break;
  }
  return RnTextAlign::Natural;
}

// React Native's weights run 100..900 and DirectWrite's do too, but this
// platform's style struct carries a bool. Anything at semibold or above is
// bold, which is the same line the GTK side draws.
bool isBold(const TextAttributes &attributes) {
  if (!attributes.fontWeight.has_value()) {
    return false;
  }
  return static_cast<int>(*attributes.fontWeight) >= static_cast<int>(facebook::react::FontWeight::Semibold);
}

// `textTransform`, applied before anything measures or draws the text.
//
// Per host rather than in a shared header, for the reason the other two are:
// Unicode case mapping belongs to the platform. `LCMapStringEx` with
// `LCMAP_LINGUISTIC_CASING` is Windows' answer, and it knows what a byte-wise
// `std::toupper` does not: that an accented letter has a case at all, and that
// a mapping can make the string longer, as the German sharp s does on the other
// two hosts. Hence the two-call form, the first asking how much room the result
// needs.
//
// `capitalize` follows React Native's own rule, from
// `RCTAttributedTextUtils.mm` and already copied on the other two hosts: split
// on single spaces, and a word whose first character is not a digit is
// capitalised, which lowercases the rest of it. So "iOS" becomes "Ios". That is
// surprising, and it is what the other platforms do.
std::wstring mapCase(const std::wstring &text, DWORD flags) {
  if (text.empty()) {
    return text;
  }
  const int needed = LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, text.c_str(),
                                   static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr, 0);
  if (needed <= 0) {
    return text;
  }
  std::wstring mapped(static_cast<size_t>(needed), L'\0');
  const int written =
      LCMapStringEx(LOCALE_NAME_USER_DEFAULT, flags, text.c_str(), static_cast<int>(text.size()),
                    mapped.data(), needed, nullptr, nullptr, 0);
  if (written <= 0) {
    return text;
  }
  mapped.resize(static_cast<size_t>(written));
  return mapped;
}

std::wstring widenUtf8(const std::string &text) {
  if (text.empty()) {
    return {};
  }
  const int needed = MultiByteToWideChar(CP_UTF8, 0, text.c_str(),
                                         static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(),
                      needed);
  return wide;
}

std::string narrowUtf8(const std::wstring &text) {
  if (text.empty()) {
    return {};
  }
  const int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                                         nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return {};
  }
  std::string narrow(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), narrow.data(),
                      needed, nullptr, nullptr);
  return narrow;
}

} // namespace

std::string transformedFragmentText(const TextAttributes &attributes, const std::string &text) {
  const auto transform = attributes.textTransform;
  if (!transform.has_value() || text.empty()) {
    return text;
  }

  // Through UTF-16 both ways, because that is what `LCMapStringEx` takes and
  // what DirectWrite counts its ranges in anyway.
  const std::wstring wide = widenUtf8(text);
  if (wide.empty()) {
    return text;
  }

  switch (*transform) {
    case facebook::react::TextTransform::None:
    case facebook::react::TextTransform::Unset:
      return text;
    case facebook::react::TextTransform::Uppercase:
      return narrowUtf8(mapCase(wide, LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING));
    case facebook::react::TextTransform::Lowercase:
      return narrowUtf8(mapCase(wide, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING));
    case facebook::react::TextTransform::Capitalize: {
      std::wstring result;
      size_t at = 0;
      bool first = true;
      while (at <= wide.size()) {
        const size_t space = wide.find(L' ', at);
        const std::wstring word =
            wide.substr(at, space == std::wstring::npos ? std::wstring::npos : space - at);
        if (!first) {
          result += L' ';
        }
        first = false;
        if (!word.empty()) {
          const std::wstring lowered = mapCase(word, LCMAP_LOWERCASE | LCMAP_LINGUISTIC_CASING);
          // A word starting with a digit is left alone, which is the rule
          // upstream's iOS half follows.
          if (!lowered.empty() && std::iswdigit(lowered.front()) != 0) {
            result += lowered;
          } else if (!lowered.empty()) {
            result += mapCase(lowered.substr(0, 1), LCMAP_UPPERCASE | LCMAP_LINGUISTIC_CASING);
            result += lowered.substr(1);
          }
        }
        if (space == std::wstring::npos) {
          break;
        }
        at = space + 1;
      }
      return narrowUtf8(result);
    }
  }
  return text;
}

// React Native's five decoration styles, as the renderer's own enum. A
// one-to-one mapping, which is the point of having the enum: this host draws
// every one of them, so there is nothing to collapse.
RnTextDecorationStyle toDecorationStyle(facebook::react::TextDecorationStyle style) {
  switch (style) {
    case facebook::react::TextDecorationStyle::Double:
      return RnTextDecorationStyle::Double;
    case facebook::react::TextDecorationStyle::Dotted:
      return RnTextDecorationStyle::Dotted;
    case facebook::react::TextDecorationStyle::Dashed:
      return RnTextDecorationStyle::Dashed;
    case facebook::react::TextDecorationStyle::Wavy:
      return RnTextDecorationStyle::Wavy;
    case facebook::react::TextDecorationStyle::Solid:
      return RnTextDecorationStyle::Solid;
  }
  return RnTextDecorationStyle::Solid;
}

RnTextStyle buildTextStyle(const TextAttributes &attributes,
                           basalt::FontFit fit,
                           std::optional<bool> rightToLeft) {
  RnTextStyle style;

  if (!attributes.fontFamily.empty()) {
    // Through the font seam, so a family an app registered at runtime resolves
    // to whatever it is actually called. An unregistered name comes back
    // unchanged and DirectWrite's own lookup gets it.
    style.fontFamily = resolveFontFamily(attributes.fontFamily);
  }

  // A NaN fontSize is React Native's "unset", and passing it to DirectWrite
  // produces a layout with no metrics at all rather than an error, so the
  // style's own default stands in for it.
  //
  // The multiplier comes from core/FontScaling.h, which is where
  // `allowFontScaling` and `maxFontSizeMultiplier` are honoured. The platform's
  // scale is 1 here: Windows does publish one, as
  // `Windows::UI::ViewManagement::UISettings::TextScaleFactor`, but reaching it
  // means WinRT and this host is Win32 all the way down.
  // backlog/platform-windows.md records that with the call named; a test can
  // supply a scale either way.
  const float requested = (!std::isnan(attributes.fontSize) && attributes.fontSize > 0)
      ? static_cast<float>(attributes.fontSize)
      : style.fontSize;
  // `adjustsFontSizeToFit` last, over the size the three font-scaling props
  // resolved to: shrinking to fit is about the size the text would otherwise
  // have been. A paragraph that never asked carries a ratio of 1 and this
  // changes nothing.
  style.fontSize = static_cast<float>(fit.apply(
      requested * basalt::effectiveFontSizeMultiplier(attributes, basalt::systemFontScale())));

  style.bold = isBold(attributes);
  style.italic = attributes.fontStyle.has_value() &&
      *attributes.fontStyle == facebook::react::FontStyle::Italic;

  if (!std::isnan(attributes.lineHeight) && attributes.lineHeight > 0) {
    style.lineHeight = static_cast<float>(attributes.lineHeight);
  }

  style.align = toAlign(attributes);

  // `letterSpacing`, which DirectWrite calls trailing character spacing: the
  // space goes after each character, as CSS's letter-spacing and iOS's kerning
  // both put it. NaN is React Native's unset and zero is the same picture, so
  // one check covers both.
  if (!std::isnan(attributes.letterSpacing)) {
    style.letterSpacing = static_cast<float>(attributes.letterSpacing);
  }

  // `writingDirection`, resolved. DirectWrite has no third state: it lays a
  // paragraph out in the reading direction it is given and never asks the text,
  // so `natural` has to be answered before this -- which core/TextDirection.h
  // does, with Unicode's rule P2, for this host and for AppKit. Without it a
  // paragraph of Hebrew with nothing set was laid out left to right, and every
  // relative alignment in it pointed at the wrong edge.
  //
  // The caller passes the paragraph's answer. The prop alone is the fallback,
  // for a caller with one fragment and no paragraph to resolve -- a
  // `<TextInput>`, whose own text direction is its own question; see
  // backlog/textinput.md.
  style.rightToLeft = rightToLeft.has_value()
      ? *rightToLeft
      : (attributes.baseWritingDirection.has_value() &&
         *attributes.baseWritingDirection == facebook::react::WritingDirection::RightToLeft);

  // `fontVariant`, as OpenType tags. core/FontVariants.h resolves the bitmask
  // and names the tags, which is what DirectWrite takes too -- the AppKit host
  // is the one that needs a translation, Core Text wanting Apple's older AAT
  // selectors.
  for (const auto variant : basalt::fontVariants(attributes)) {
    if (const char *const tag = basalt::openTypeTag(variant); tag != nullptr) {
      style.fontFeatures.emplace_back(tag);
    }
  }

  // `textDecorationLine`, `textDecorationColor` and `textDecorationStyle`, all
  // three resolved by core/TextDecorations.h so that the hosts agree on what an
  // app asked for.
  //
  // All three arrive here, where two of them used to be dropped: a DirectWrite
  // underline is a boolean per range, so the colour and the style are carried
  // to the custom renderer in RnWin32TextLayout.cpp and drawn there. Which
  // leaves this the one host that draws all five styles -- Core Text has no
  // wavy and Pango has no dotted or dashed, and both of those fall back to a
  // single line.
  if (const auto decoration = basalt::textDecoration(attributes)) {
    style.underline = decoration->underline;
    style.strikethrough = decoration->strikethrough;
    style.decorationStyle = toDecorationStyle(decoration->style);
    if (decoration->hasColor) {
      style.hasDecorationColour = true;
      style.decorationColour[0] = decoration->red;
      style.decorationColour[1] = decoration->green;
      style.decorationColour[2] = decoration->blue;
      style.decorationColour[3] = decoration->alpha;
    }
  }

  // Through core/TextColors.h rather than straight off the prop, which is what
  // makes a fragment's `opacity` arrive: it multiplies the alpha, and this host
  // was the one still reading the colour itself. The default is opaque black
  // there too, so nothing has to spell one here.
  const basalt::TextColor foreground = basalt::textForegroundColor(attributes);
  style.color[0] = foreground.red;
  style.color[1] = foreground.green;
  style.color[2] = foreground.blue;
  style.color[3] = foreground.alpha;

  // A fragment's own `backgroundColor`, from the same header: an unset one is
  // nothing rather than transparent black, which is the distinction that keeps
  // a box from being painted behind text that asked for none.
  //
  // DirectWrite draws no background at all -- it draws glyphs -- so this is the
  // other half of what the custom renderer is for, and it fills the box in
  // `DrawGlyphRun`.
  if (const auto background = basalt::textBackgroundColor(attributes)) {
    style.hasBackgroundColour = true;
    style.backgroundColour[0] = background->red;
    style.backgroundColour[1] = background->green;
    style.backgroundColour[2] = background->blue;
    style.backgroundColour[3] = background->alpha;
  }

  return style;
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
        const auto layout = buildTextLayout(attributedString, untruncated, probe);
        if (layout == nullptr) {
          return basalt::FontFitBox{};
        }
        const RnTextSize size = layout->measure(maxWidth);
        return basalt::FontFitBox{.width = size.width, .height = size.height};
      },
      basalt::FontFitBox{.width = maxWidth, .height = maxHeight});
}

std::shared_ptr<RnWin32TextLayout>
buildTextLayout(const AttributedString &attributedString,
                const ParagraphAttributes &paragraphAttributes,
                basalt::FontFit fit) {
  // Which way the paragraph runs, once for the whole of it rather than per
  // fragment: `writingDirection` is a paragraph's prop, and the text it is
  // resolved against is the paragraph's text. See core/TextDirection.h.
  const bool rightToLeft = basalt::paragraphIsRightToLeft(
      basalt::writingDirection(attributedString), basalt::paragraphText(attributedString));

  std::vector<RnTextRun> runs;
  for (const auto &fragment : attributedString.getFragments()) {
    // An attachment is an inline `<View>`, which occupies space rather than
    // carrying text. It does contribute a run: its string is React Native's
    // placeholder character, which is what the inline box is attached to, and
    // the box is the frame the view was already laid out with -- so the
    // paragraph reserves the room and `attachmentBoxes` reads back where it
    // landed. The other two desktops do the same with a Pango shape attribute
    // and a Core Text run delegate.
    //
    // The placeholder is never drawn: the inline object draws nothing and the
    // mounting manager paints the view itself.
    if (fragment.isAttachment()) {
      const auto &size = fragment.parentShadowView.layoutMetrics.frame.size;
      RnTextRun run{
          fragment.string, buildTextStyle(fragment.textAttributes, fit, rightToLeft), std::nullopt};
      run.inlineBox =
          RnInlineBox{static_cast<float>(size.width), static_cast<float>(size.height)};
      runs.push_back(std::move(run));
      continue;
    }
    // The transformed text, because a `textTransform` has to happen before
    // anything measures the string: this file is what both the measurement seam
    // and the mounting manager go through, so doing it here is what keeps the
    // two agreeing.
    runs.push_back(RnTextRun{transformedFragmentText(fragment.textAttributes, fragment.string),
                             buildTextStyle(fragment.textAttributes, fit, rightToLeft)});
  }

  auto layout = RnWin32TextLayout::createFromRuns(
      runs, static_cast<int>(paragraphAttributes.maximumNumberOfLines));

  // The text shadow, which `core/TextShadows.h` resolves: one per paragraph,
  // from the first fragment that asks for one, because no engine here can draw
  // a different shadow per run. The standard deviation it answers with is what
  // Direct2D's shadow effect takes, so nothing converts.
  if (layout != nullptr) {
    if (const auto shadow = basalt::textShadow(attributedString)) {
      const float colour[4] = {shadow->red, shadow->green, shadow->blue, shadow->alpha};
      layout->setShadow(shadow->dx, shadow->dy, shadow->standardDeviation, colour);
    }
  }

  return layout;
}

} // namespace basalt::win32
