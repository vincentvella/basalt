#include "DirectWriteLayout.h"

#include "FontScaling.h"

#include "FontRegistry.h"
#include "TextDecorations.h"

#include <windows.h>

#include <cwctype>
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
    return RnTextAlign::Left;
  }
  switch (*attributes.alignment) {
    case facebook::react::TextAlignment::Center:
      return RnTextAlign::Center;
    case facebook::react::TextAlignment::Right:
#if BASALT_RN_MINOR >= 87
    // React Native 0.87 added the writing-direction-relative spellings, and
    // both other desktops already fold them in here. Left out of this switch,
    // `textAlign: 'right'` written as `end` fell through to left-aligned --
    // which clang said out loud, in a warning nobody had recompiled this file
    // to see.
    //
    // Left-to-right only, like the other two: nothing on any of these
    // platforms resolves a writing direction yet.
    case facebook::react::TextAlignment::End:
#endif
      return RnTextAlign::Right;
    case facebook::react::TextAlignment::Justified:
      return RnTextAlign::Justified;
    case facebook::react::TextAlignment::Natural:
    case facebook::react::TextAlignment::Left:
#if BASALT_RN_MINOR >= 87
    case facebook::react::TextAlignment::Start:
#endif
      break;
  }
  return RnTextAlign::Left;
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

RnTextStyle buildTextStyle(const TextAttributes &attributes) {
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
  style.fontSize =
      requested * basalt::effectiveFontSizeMultiplier(attributes, basalt::systemFontScale());

  style.bold = isBold(attributes);
  style.italic = attributes.fontStyle.has_value() &&
      *attributes.fontStyle == facebook::react::FontStyle::Italic;

  if (!std::isnan(attributes.lineHeight) && attributes.lineHeight > 0) {
    style.lineHeight = static_cast<float>(attributes.lineHeight);
  }

  style.align = toAlign(attributes);

  // `textDecorationLine`, resolved by core/TextDecorations.h so that the three
  // hosts agree on which lines an app asked for. The style and the colour are
  // resolved there too and dropped here: a DirectWrite underline is a boolean
  // per range, and anything more needs a custom renderer. Falling back to a
  // solid line in the text's colour rather than drawing nothing, which is the
  // choice that header argues for.
  if (const auto decoration = basalt::textDecoration(attributes)) {
    style.underline = decoration->underline;
    style.strikethrough = decoration->strikethrough;
  }

  if (attributes.foregroundColor) {
    const auto components = facebook::react::colorComponentsFromColor(attributes.foregroundColor);
    style.color[0] = components.red;
    style.color[1] = components.green;
    style.color[2] = components.blue;
    style.color[3] = components.alpha;
  }

  return style;
}

std::shared_ptr<RnWin32TextLayout>
buildTextLayout(const AttributedString &attributedString,
                const ParagraphAttributes &paragraphAttributes) {
  std::vector<RnTextRun> runs;
  for (const auto &fragment : attributedString.getFragments()) {
    // An attachment is an inline `<View>`, which occupies space rather than
    // carrying text. Its string is a placeholder React Native does not intend
    // to be drawn, so it contributes no run -- which is also why the attachment
    // rects the layout manager reports are all zero, and why nothing reserves
    // room for one here. The other two desktops reserve it and report it back;
    // see docs/backlog/text.md for what this one would take.
    if (fragment.isAttachment()) {
      continue;
    }
    // The transformed text, because a `textTransform` has to happen before
    // anything measures the string: this file is what both the measurement seam
    // and the mounting manager go through, so doing it here is what keeps the
    // two agreeing.
    runs.push_back(RnTextRun{transformedFragmentText(fragment.textAttributes, fragment.string),
                             buildTextStyle(fragment.textAttributes)});
  }

  return RnWin32TextLayout::createFromRuns(
      runs, static_cast<int>(paragraphAttributes.maximumNumberOfLines));
}

} // namespace basalt::win32
