#include "DirectWriteLayout.h"

#include "FontRegistry.h"

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

} // namespace

RnTextStyle buildTextStyle(const TextAttributes &attributes) {
  RnTextStyle style;

  if (!attributes.fontFamily.empty()) {
    // Through the font seam, so a family an app registered at runtime resolves
    // to whatever it is actually called. An unregistered name comes back
    // unchanged and DirectWrite's own lookup gets it.
    style.fontFamily = resolveFontFamily(attributes.fontFamily);
  }

  // A NaN fontSize is React Native's "unset", and passing it to DirectWrite
  // produces a layout with no metrics at all rather than an error.
  if (!std::isnan(attributes.fontSize) && attributes.fontSize > 0) {
    style.fontSize = static_cast<float>(attributes.fontSize);
  }

  style.bold = isBold(attributes);
  style.italic = attributes.fontStyle.has_value() &&
      *attributes.fontStyle == facebook::react::FontStyle::Italic;

  if (!std::isnan(attributes.lineHeight) && attributes.lineHeight > 0) {
    style.lineHeight = static_cast<float>(attributes.lineHeight);
  }

  style.align = toAlign(attributes);

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
    // rects the layout manager reports are all zero. See docs/BACKLOG.md.
    if (fragment.isAttachment()) {
      continue;
    }
    runs.push_back(RnTextRun{fragment.string, buildTextStyle(fragment.textAttributes)});
  }

  return RnWin32TextLayout::createFromRuns(
      runs, static_cast<int>(paragraphAttributes.maximumNumberOfLines));
}

} // namespace basalt::win32
