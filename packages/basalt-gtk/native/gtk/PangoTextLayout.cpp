#include "PangoTextLayout.h"

#include "FontRegistry.h"

#include <react/renderer/graphics/Color.h>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>

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

// Pango's own default family is a serif face, which is not what React Native
// means by "no fontFamily". "Sans" is fontconfig's alias for the system
// sans-serif, the closest equivalent to the system font iOS and Android use.
constexpr const char *kDefaultFontFamily = "Sans";

// Pango works in 1/1024ths of a pixel. Everything crossing this boundary goes
// through these two, so no raw multiplication by PANGO_SCALE appears below.
int toPangoUnits(float points) {
  return static_cast<int>(std::lround(points * PANGO_SCALE));
}

float fromPangoUnits(int units) {
  return static_cast<float>(units) / PANGO_SCALE;
}

bool isSet(facebook::react::Float value) {
  return !std::isnan(value);
}

// Pango's colour channels are 16-bit; React Native's are floats in 0..1.
guint16 toPangoChannel(float component) {
  return static_cast<guint16>(std::lround(std::clamp(component, 0.0F, 1.0F) * 65535.0F));
}

PangoWeight toPangoWeight(FontWeight weight) {
  // The enum's values are the CSS numeric weights, and Pango's are the same
  // scale, so this is a straight cast rather than a mapping table.
  return static_cast<PangoWeight>(static_cast<int>(weight));
}

PangoStyle toPangoStyle(FontStyle style) {
  switch (style) {
    case FontStyle::Italic:
      return PANGO_STYLE_ITALIC;
    case FontStyle::Oblique:
      return PANGO_STYLE_OBLIQUE;
    case FontStyle::Normal:
      break;
  }
  return PANGO_STYLE_NORMAL;
}

// Start and End are the writing-direction-relative alignments, and they arrived
// in React Native 0.87. Before that the enum has Natural, Left, Center, Right
// and Justified, and naming the others is a compile error rather than a dead
// branch. The threshold was guessed at 0.83 first and 0.86 disproved it, which
// is why it is now checked against the tags rather than assumed.
PangoAlignment toPangoAlignment(TextAlignment alignment) {
  switch (alignment) {
    case TextAlignment::Center:
      return PANGO_ALIGN_CENTER;
    case TextAlignment::Right:
#if BASALT_RN_MINOR >= 87
    case TextAlignment::End:
#endif
      return PANGO_ALIGN_RIGHT;
    case TextAlignment::Left:
#if BASALT_RN_MINOR >= 87
    case TextAlignment::Start:
#endif
    case TextAlignment::Natural:
    case TextAlignment::Justified:
      break;
  }
  // Justified is expressed separately, through pango_layout_set_justify.
  return PANGO_ALIGN_LEFT;
}

PangoEllipsizeMode toPangoEllipsize(EllipsizeMode mode) {
  switch (mode) {
    case EllipsizeMode::Head:
      return PANGO_ELLIPSIZE_START;
    case EllipsizeMode::Middle:
      return PANGO_ELLIPSIZE_MIDDLE;
    case EllipsizeMode::Tail:
      return PANGO_ELLIPSIZE_END;
    case EllipsizeMode::Clip:
      break;
  }
  return PANGO_ELLIPSIZE_NONE;
}

// A PangoContext to lay out against. Created from the default cairo font map,
// not from a GtkWidget: measurement happens on Fabric's layout thread, where
// there is no widget, and painting must use the same font map or the two would
// disagree about metrics.
//
// Pango's font map is not documented as reentrant, and this is reached from the
// layout thread and the GTK main thread, so one mutex covers every use.
std::mutex &pangoMutex() {
  static std::mutex mutex;
  return mutex;
}

PangoContext *sharedPangoContext() {
  static PangoContext *context = pango_font_map_create_context(pango_cairo_font_map_get_default());
  return context;
}

void applyFragmentAttributes(PangoAttrList *attributes,
                             const TextAttributes &textAttributes,
                             guint startIndex,
                             guint endIndex) {
  const auto addAttribute = [&](PangoAttribute *attribute) {
    attribute->start_index = startIndex;
    attribute->end_index = endIndex;
    pango_attr_list_insert(attributes, attribute);
  };

  PangoFontDescription *font = pango_font_description_new();

  // Through the runtime registry: a font an app loaded at runtime is known to
  // the app by a name it chose, and to fontconfig by the name inside the file.
  // resolveFontFamily returns the argument unchanged for ordinary system
  // families, so this costs a lookup and changes nothing for them.
  const std::string family = textAttributes.fontFamily.empty()
      ? std::string{kDefaultFontFamily}
      : resolveFontFamily(textAttributes.fontFamily);
  pango_font_description_set_family(font, family.c_str());

  float fontSize = isSet(textAttributes.fontSize) ? static_cast<float>(textAttributes.fontSize) : kDefaultFontSize;
  if (isSet(textAttributes.fontSizeMultiplier) && textAttributes.fontSizeMultiplier > 0) {
    fontSize *= static_cast<float>(textAttributes.fontSizeMultiplier);
  }
  // set_absolute_size, not set_size. set_size takes points and resolves them
  // against the context's resolution, which at the default 96dpi would render
  // a fontSize of 16 at about 21px. React Native's fontSize is in
  // density-independent pixels, and every coordinate on this platform -- Yoga's
  // frames, the widget's allocation -- is in that same logical space, so the
  // size is a device-unit size and must bypass dpi entirely.
  pango_font_description_set_absolute_size(font, toPangoUnits(fontSize));

  if (textAttributes.fontWeight) {
    pango_font_description_set_weight(font, toPangoWeight(*textAttributes.fontWeight));
  }
  if (textAttributes.fontStyle) {
    pango_font_description_set_style(font, toPangoStyle(*textAttributes.fontStyle));
  }

  addAttribute(pango_attr_font_desc_new(font));
  pango_font_description_free(font);

  if (textAttributes.foregroundColor) {
    const auto components = colorComponentsFromColor(textAttributes.foregroundColor);
    addAttribute(pango_attr_foreground_new(toPangoChannel(components.red),
                                           toPangoChannel(components.green),
                                           toPangoChannel(components.blue)));
    addAttribute(pango_attr_foreground_alpha_new(toPangoChannel(components.alpha)));
  }

  if (textAttributes.backgroundColor) {
    const auto components = colorComponentsFromColor(textAttributes.backgroundColor);
    addAttribute(pango_attr_background_new(toPangoChannel(components.red),
                                           toPangoChannel(components.green),
                                           toPangoChannel(components.blue)));
    addAttribute(pango_attr_background_alpha_new(toPangoChannel(components.alpha)));
  }

  if (isSet(textAttributes.letterSpacing)) {
    addAttribute(pango_attr_letter_spacing_new(toPangoUnits(static_cast<float>(textAttributes.letterSpacing))));
  }

  // React Native's lineHeight is the total line box height, which is what
  // Pango's absolute line height means too. Same logical units as fontSize.
  if (isSet(textAttributes.lineHeight)) {
    addAttribute(
        pango_attr_line_height_new_absolute(toPangoUnits(static_cast<float>(textAttributes.lineHeight))));
  }

  if (textAttributes.textDecorationLineType) {
    switch (*textAttributes.textDecorationLineType) {
      case TextDecorationLineType::Underline:
        addAttribute(pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
        break;
      case TextDecorationLineType::Strikethrough:
        addAttribute(pango_attr_strikethrough_new(TRUE));
        break;
      case TextDecorationLineType::UnderlineStrikethrough:
        addAttribute(pango_attr_underline_new(PANGO_UNDERLINE_SINGLE));
        addAttribute(pango_attr_strikethrough_new(TRUE));
        break;
      case TextDecorationLineType::None:
        break;
    }
  }
}

} // namespace

PangoLayout *buildTextLayout(const AttributedString &attributedString,
                             const ParagraphAttributes &paragraphAttributes,
                             float maxWidth) {
  const std::lock_guard<std::mutex> lock(pangoMutex());

  PangoLayout *layout = pango_layout_new(sharedPangoContext());
  PangoAttrList *attributes = pango_attr_list_new();

  // Fragment ranges are byte offsets into the concatenated UTF-8 string, which
  // is the same unit Pango's attribute indices use.
  std::string text;
  for (const auto &fragment : attributedString.getFragments()) {
    const auto start = static_cast<guint>(text.size());
    text += fragment.string;
    const auto end = static_cast<guint>(text.size());
    if (end > start) {
      applyFragmentAttributes(attributes, fragment.textAttributes, start, end);
    }
  }

  pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
  pango_layout_set_attributes(layout, attributes);
  pango_attr_list_unref(attributes);

  // Paragraph-level settings come from the first fragment, since React Native
  // resolves alignment onto every fragment from the <Text> that owns them.
  const auto &fragments = attributedString.getFragments();
  if (!fragments.empty() && fragments.front().textAttributes.alignment) {
    const auto alignment = *fragments.front().textAttributes.alignment;
    pango_layout_set_alignment(layout, toPangoAlignment(alignment));
    pango_layout_set_justify(layout, alignment == TextAlignment::Justified);
  }

  if (maxWidth >= 0) {
    pango_layout_set_width(layout, toPangoUnits(maxWidth));
    pango_layout_set_wrap(layout, PANGO_WRAP_WORD_CHAR);
  } else {
    pango_layout_set_width(layout, -1);
  }

  // Ellipsization is only meaningful with a line limit, and turning it on
  // without one does not mean "ellipsize if it overflows" -- with no height
  // set, Pango ellipsizes to a *single* line, which silently collapses every
  // wrapping paragraph to one line. React Native's ellipsizeMode defaults to
  // Tail, so this guard is what lets ordinary text wrap at all.
  if (paragraphAttributes.maximumNumberOfLines > 0) {
    pango_layout_set_ellipsize(layout, toPangoEllipsize(paragraphAttributes.ellipsizeMode));
    // A negative height is Pango's way of expressing a line count. It only
    // truncates when ellipsization is on, so numberOfLines with ellipsizeMode
    // 'clip' still overflows; that wants a clip in the widget instead.
    pango_layout_set_height(layout, -paragraphAttributes.maximumNumberOfLines);
  } else {
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
  }

  return layout;
}

PangoAttrList *buildTextAttributes(const TextAttributes &textAttributes) {
  const std::lock_guard<std::mutex> lock(pangoMutex());

  PangoAttrList *attributes = pango_attr_list_new();
  // G_MAXUINT is Pango's "to the end", so the list stays correct as the user
  // types and the string it covers grows.
  applyFragmentAttributes(attributes, textAttributes, 0, G_MAXUINT);
  return attributes;
}

void textLayoutSize(PangoLayout *layout, float *outWidth, float *outHeight) {
  const std::lock_guard<std::mutex> lock(pangoMutex());

  int width = 0;
  int height = 0;
  pango_layout_get_size(layout, &width, &height);
  *outWidth = fromPangoUnits(width);
  *outHeight = fromPangoUnits(height);
}

} // namespace basalt
