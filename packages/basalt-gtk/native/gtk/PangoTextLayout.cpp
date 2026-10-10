#include "PangoTextLayout.h"

#include "TextAlignments.h"
#include "TextDirection.h"

#include "FontRegistry.h"
#include "FontScaling.h"
#include "FontVariants.h"
#include "TextColors.h"
#include "TextDecorations.h"
// For rn_pango_clip_height, which is how much of a paragraph `numberOfLines`
// leaves visible. It lives with the widgets because the widget layer has no
// React Native in it and so cannot include this header; the dependency only
// runs this way round. See the comment on it in RnView.h.
#include "RnView.h"

#include <react/renderer/graphics/Color.h>

#include <algorithm>
#include <cmath>
#include <thread>
#include <optional>
#include <string>

namespace basalt {

using facebook::react::AttributedString;
using facebook::react::EllipsizeMode;
using facebook::react::FontStyle;
using facebook::react::FontWeight;
using facebook::react::ParagraphAttributes;
using facebook::react::TextAlignment;
using facebook::react::TextTransform;
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

// React Native's five decoration styles against the three Pango has.
//
// `ERROR` is the wavy one a spell checker draws, which is what `wavy` means, so
// that pair is exact. Dotted and dashed have no Pango value at all -- the enum
// carries no patterns -- and fall back to a single line, which is visible and
// wrong rather than missing and invisible. core/TextDecorations.h says why that
// is the choice, and backlog/text.md names the call that would close it.
PangoUnderline toPangoUnderline(facebook::react::TextDecorationStyle style) {
  switch (style) {
    case facebook::react::TextDecorationStyle::Double:
      return PANGO_UNDERLINE_DOUBLE;
    case facebook::react::TextDecorationStyle::Wavy:
      return PANGO_UNDERLINE_ERROR;
    case facebook::react::TextDecorationStyle::Solid:
    case facebook::react::TextDecorationStyle::Dotted:
    case facebook::react::TextDecorationStyle::Dashed:
      return PANGO_UNDERLINE_SINGLE;
  }
  return PANGO_UNDERLINE_SINGLE;
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

// An edge, spelled as Pango spells it. The relative alignments are gone by
// here: `core/TextAlignments.h` resolved them against the paragraph's
// direction, and `Justified` is expressed separately through
// `pango_layout_set_justify` -- so what arrives is one of three.
PangoAlignment toPangoAlignment(basalt::PhysicalTextAlignment alignment) {
  switch (alignment) {
    case basalt::PhysicalTextAlignment::Center:
      return PANGO_ALIGN_CENTER;
    case basalt::PhysicalTextAlignment::Right:
      return PANGO_ALIGN_RIGHT;
    case basalt::PhysicalTextAlignment::Left:
    case basalt::PhysicalTextAlignment::Justified:
      break;
  }
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

// A PangoContext to lay out against, one per thread.
//
// Created from the default cairo font map rather than from a GtkWidget, because
// measurement happens on Fabric's layout thread where there is no widget, and
// painting must resolve the same families at the same metrics or Yoga would allot
// a box computed one way and the widget would paint text laid out another.
//
// **Per thread, and that is the whole of the thread safety here.** This used to
// be one `static` context guarded by a mutex, on the stated grounds that Pango's
// font map is not documented as reentrant. The premise was wrong in a way that
// made the code less safe rather than more: Pango's own answer to this question
// is isolation, not locking. From its documentation, "since Pango 1.32.6, the
// default fontmap is per-thread. Each thread gets its own default fontmap. In
// this way, PangoCairo can be used safely from multiple threads." A single
// static context, built from whichever thread reached it first, opted out of
// that and then needed a lock to put back what it had given away. The shared
// font map the mutex protected was self-inflicted.
//
// The lock was also never complete. It covered building and measuring and not
// painting, and painting is where the work actually happens: a layout handed to
// a widget has not been laid out yet, so `gtk_snapshot_append_layout` is what
// itemises, resolves fonts and shapes, on the main thread, outside any lock. See
// backlog/text.md.
//
// What this costs: one font map, glyph cache and face set per thread, bounded at
// two. What it buys, besides correctness, is that text measurement no longer
// serialises on a process-wide mutex.
//
// What it requires, and `buildTextLayout` asserts it: a layout must be painted by
// the thread that built it. That holds today because `GtkMountingManager` builds
// its layouts on the main thread and the measuring path never lets one escape the
// function, but it holds incidentally rather than by construction.
// One per thread *per base direction*, which is three.
//
// `baseWritingDirection` is a paragraph's direction, and Pango keeps a base
// direction on the **context** rather than on the layout: a layout with
// `auto_dir` off takes its context's. Setting it on the one shared context
// before each build and putting it back afterwards would be a race with
// measurement, which happens later and off this thread -- Pango resolves
// direction when the extents are asked for, not when the text is set.
//
// So each direction gets its own context, cached beside the other two. The cost
// is two more font maps per thread in an app that uses them; the alternative is
// a paragraph whose direction depends on what was measured after it.
PangoContext *threadPangoContext(PangoDirection direction) {
  // Reloaded when a font is registered at runtime. Each thread's map is
  // invalidated separately, which is the price of not sharing one, and
  // `fontGeneration` is what makes that detectable here. Without this a family
  // that appeared after a thread's first measurement would be missing from that
  // thread's map for the life of the process.
  static thread_local PangoContext *contexts[3] = {nullptr, nullptr, nullptr};
  static thread_local unsigned long generation = 0;

  const int slot = direction == PANGO_DIRECTION_LTR ? 1
      : direction == PANGO_DIRECTION_RTL            ? 2
                                                    : 0;

  const unsigned long current = basalt::fontGeneration();
  if (current != generation) {
    for (PangoContext *&cached : contexts) {
      g_clear_object(&cached);
    }
    generation = current;
  }
  if (contexts[slot] == nullptr) {
    contexts[slot] = pango_font_map_create_context(pango_cairo_font_map_get_default());
    if (slot != 0) {
      pango_context_set_base_dir(contexts[slot], direction);
    }
  }
  return contexts[slot];
}

// React Native's three writing directions against Pango's.
//
// `Natural` is not a direction here: it asks for the Unicode bidi algorithm's
// answer, which Pango gives through `auto_dir` on the layout rather than a base
// direction on the context. So it maps to neutral and the caller turns auto
// direction on; see buildTextLayout.
PangoDirection toPangoDirection(std::optional<facebook::react::WritingDirection> direction) {
  if (!direction.has_value()) {
    return PANGO_DIRECTION_NEUTRAL;
  }
  switch (*direction) {
    case facebook::react::WritingDirection::LeftToRight:
      return PANGO_DIRECTION_LTR;
    case facebook::react::WritingDirection::RightToLeft:
      return PANGO_DIRECTION_RTL;
    case facebook::react::WritingDirection::Natural:
      return PANGO_DIRECTION_NEUTRAL;
  }
  return PANGO_DIRECTION_NEUTRAL;
}


void applyFragmentAttributes(PangoAttrList *attributes,
                             const TextAttributes &textAttributes,
                             guint startIndex,
                             guint endIndex,
                             basalt::FontFit fit = {}) {
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

  // Through core/FontScaling.h, which is where `allowFontScaling` and
  // `maxFontSizeMultiplier` are honoured: this used to multiply by
  // `fontSizeMultiplier` and read neither of the two props that exist to
  // control that multiplication.
  // `adjustsFontSizeToFit` last, over the size the three font-scaling props
  // resolved to: shrinking to fit is about the size the text would otherwise
  // have been, including the desktop's own text scale. A paragraph that never
  // asked carries a ratio of 1 and this changes nothing.
  const float fontSize = static_cast<float>(fit.apply(
      basalt::effectiveFontSize(textAttributes, kDefaultFontSize, basalt::systemFontScale())));
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

  // `fontVariant`, as the feature string Pango takes: `smcp=1,tnum=1`. The
  // flags and their OpenType tags are core/FontVariants.h's, so GTK and Win32
  // ask for the same features in the same order and AppKit maps the same flags
  // to Core Text's older selectors.
  const std::string features = basalt::fontFeatureSettings(textAttributes);
  if (!features.empty()) {
    addAttribute(pango_attr_font_features_new(features.c_str()));
  }

  // The foreground, with `opacity` already multiplied in: core/TextColors.h has
  // that rule, React Native's default of opaque black with it. An unset colour
  // still needs the attribute now, because opacity alone changes it.
  {
    const basalt::TextColor color = basalt::textForegroundColor(textAttributes);
    if (textAttributes.foregroundColor || color.alpha < 1.0F) {
      addAttribute(pango_attr_foreground_new(toPangoChannel(color.red),
                                             toPangoChannel(color.green),
                                             toPangoChannel(color.blue)));
      addAttribute(pango_attr_foreground_alpha_new(toPangoChannel(color.alpha)));
    }
  }

  // And the background, which stays nothing when nothing asked for one: an
  // opacity of a colour that does not exist is still no colour.
  if (const auto background = basalt::textBackgroundColor(textAttributes)) {
    addAttribute(pango_attr_background_new(toPangoChannel(background->red),
                                           toPangoChannel(background->green),
                                           toPangoChannel(background->blue)));
    addAttribute(pango_attr_background_alpha_new(toPangoChannel(background->alpha)));
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

  // Which lines, in which colour, in which style: resolved in
  // core/TextDecorations.h so that both hosts agree about what was asked for,
  // and mapped here to what Pango has.
  if (const auto decoration = basalt::textDecoration(textAttributes)) {
    if (decoration->underline) {
      addAttribute(pango_attr_underline_new(toPangoUnderline(decoration->style)));
    }
    if (decoration->strikethrough) {
      // A boolean, with no style of its own: Pango draws one line through the
      // text and takes no pattern for it. backlog/text.md records that.
      addAttribute(pango_attr_strikethrough_new(TRUE));
    }
    if (decoration->hasColor) {
      const guint16 red = toPangoChannel(decoration->red);
      const guint16 green = toPangoChannel(decoration->green);
      const guint16 blue = toPangoChannel(decoration->blue);
      // Per line, because Pango has one attribute for each and no shared
      // "decoration colour". An alpha attribute would apply to the glyphs as
      // well, so a translucent decoration colour is recorded rather than
      // half-applied.
      if (decoration->underline) {
        addAttribute(pango_attr_underline_color_new(red, green, blue));
      }
      if (decoration->strikethrough) {
        addAttribute(pango_attr_strikethrough_color_new(red, green, blue));
      }
    }
  }
}

// An inline `<View>` inside a `<Text>`, which React Native represents as one
// fragment holding U+FFFC, the object replacement character.
//
// Without this the character is in the string and reserves whatever width the
// font happens to give a missing glyph, so a 40x20 view inside a sentence took
// about a space and the view itself was reported at zero size. A shape attribute
// is Pango's way to say "this range is not text, it occupies this box": the glyph
// is not drawn, the line reserves the space, and wrapping and alignment treat it
// as one unbreakable run.
//
// The size comes from React Native, not from here. `ParagraphShadowNode`
// measures each attachment's own shadow node and writes the result into the
// fragment's `parentShadowView.layoutMetrics` before calling `measure`, so by
// now it is known and this only has to honour it.
//
// Both rects are the same box. The ink rect is what would be painted and the
// logical rect is what the line reserves; for a view they are the same thing,
// and anchoring at the baseline (y from -height to 0) is what makes a tall view
// sit on the line rather than hang below it.
void applyAttachmentShape(PangoAttrList *attributes,
                          const AttributedString::Fragment &fragment,
                          guint startIndex,
                          guint endIndex) {
  const auto &size = fragment.parentShadowView.layoutMetrics.frame.size;
  if (size.width <= 0 || size.height <= 0) {
    // Nothing measured it, so there is no box to reserve and a shape of zero
    // would hide the character without putting anything in its place.
    return;
  }

  PangoRectangle box = {};
  box.x = 0;
  box.y = -static_cast<int>(size.height * PANGO_SCALE);
  box.width = static_cast<int>(size.width * PANGO_SCALE);
  box.height = static_cast<int>(size.height * PANGO_SCALE);

  PangoAttribute *shape = pango_attr_shape_new(&box, &box);
  shape->start_index = startIndex;
  shape->end_index = endIndex;
  pango_attr_list_insert(attributes, shape);
}

} // namespace

// `textTransform`, applied before anything measures or draws the text.
//
// Here rather than in a shared header because Unicode case mapping is the
// toolkit's: `g_utf8_strup` knows that ß uppercases to SS and that an accented
// letter has a case at all, where a byte-wise `std::toupper` would leave both
// alone and look right in English. The AppKit host uses NSString's for the same
// reason, and the two are asserted against the same cases.
//
// `capitalize` follows React Native's own rule, from
// RCTAttributedTextUtils.mm: split on single spaces, and a word whose first
// character is not a digit is capitalised -- which lowercases the rest of it, so
// "iOS" becomes "Ios". That is surprising and is what the other platforms do.
static std::string transformedFragmentText(const AttributedString::Fragment &fragment) {
  const auto transform = fragment.textAttributes.textTransform;
  if (!transform.has_value() || fragment.string.empty()) {
    return fragment.string;
  }
  switch (*transform) {
    case TextTransform::Uppercase: {
      char *upper = g_utf8_strup(fragment.string.c_str(), -1);
      std::string result(upper != nullptr ? upper : fragment.string.c_str());
      g_free(upper);
      return result;
    }
    case TextTransform::Lowercase: {
      char *lower = g_utf8_strdown(fragment.string.c_str(), -1);
      std::string result(lower != nullptr ? lower : fragment.string.c_str());
      g_free(lower);
      return result;
    }
    case TextTransform::Capitalize: {
      std::string result;
      gchar **words = g_strsplit(fragment.string.c_str(), " ", -1);
      for (int i = 0; words != nullptr && words[i] != nullptr; i++) {
        if (i > 0) {
          result += " ";
        }
        if (*words[i] == '\0') {
          continue;
        }
        const gunichar first = g_utf8_get_char(words[i]);
        char *lower = g_utf8_strdown(words[i], -1);
        const char *body = lower != nullptr ? lower : words[i];
        if (g_unichar_isdigit(first)) {
          result += body;
        } else {
          // The first character uppercased and the rest left lowered, which is
          // what NSString's capitalizedString does word by word.
          char *firstUpper = g_utf8_strup(body, g_utf8_next_char(body) - body);
          result += firstUpper != nullptr ? firstUpper : "";
          result += g_utf8_next_char(body);
          g_free(firstUpper);
        }
        g_free(lower);
      }
      g_strfreev(words);
      return result;
    }
    case TextTransform::None:
    case TextTransform::Unset:
      break;
  }
  return fragment.string;
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
        PangoLayout *layout = buildTextLayout(attributedString, untruncated, maxWidth, probe);
        float width = 0;
        float height = 0;
        textLayoutSize(layout, &width, &height);
        g_object_unref(layout);
        return basalt::FontFitBox{.width = width, .height = height};
      },
      basalt::FontFitBox{.width = maxWidth, .height = maxHeight});
}

PangoLayout *buildTextLayout(const AttributedString &attributedString,
                             const ParagraphAttributes &paragraphAttributes,
                             float maxWidth,
                             basalt::FontFit fit) {

  // `baseWritingDirection`, which decides the context this layout is built on
  // and therefore has to be read before the layout exists. From the first
  // fragment, like the alignment below: React Native resolves a paragraph's
  // attributes onto every fragment in it.
  const auto &first = attributedString.getFragments();
  const auto direction = first.empty()
      ? std::optional<facebook::react::WritingDirection>{}
      : first.front().textAttributes.baseWritingDirection;

  PangoLayout *layout = pango_layout_new(threadPangoContext(toPangoDirection(direction)));
  // Natural means the Unicode bidi algorithm decides from the first strong
  // character, which is what Pango calls auto direction and has on by default.
  // Anything else is an app overriding that, so auto direction goes off and the
  // context's base direction stands.
  //
  // Remembered rather than passed straight in, because the alignment below
  // needs to know which of the two this is: auto direction changes what
  // ALIGN_LEFT means.
  const bool autoDirection =
      !direction.has_value() || *direction == facebook::react::WritingDirection::Natural;
  pango_layout_set_auto_dir(layout, autoDirection);
  PangoAttrList *attributes = pango_attr_list_new();

  // Fragment ranges are byte offsets into the concatenated UTF-8 string, which
  // is the same unit Pango's attribute indices use.
  std::string text;
  for (const auto &fragment : attributedString.getFragments()) {
    const auto start = static_cast<guint>(text.size());
    text += transformedFragmentText(fragment);
    const auto end = static_cast<guint>(text.size());
    if (end > start) {
      applyFragmentAttributes(attributes, fragment.textAttributes, start, end, fit);
    }
    if (fragment.isAttachment() && end > start) {
      applyAttachmentShape(attributes, fragment, start, end);
    }
  }

  pango_layout_set_text(layout, text.c_str(), static_cast<int>(text.size()));
  pango_layout_set_attributes(layout, attributes);
  pango_attr_list_unref(attributes);

  // Paragraph-level settings come from the first fragment, since React Native
  // resolves alignment onto every fragment from the <Text> that owns them.
  const auto &fragments = attributedString.getFragments();
  const std::optional<TextAlignment> alignment = fragments.empty()
      ? std::optional<TextAlignment>{}
      : fragments.front().textAttributes.alignment;

  // Which way the paragraph runs, which every relative alignment needs an
  // answer to: the prop when the app set one, and Unicode's rule P2 over the
  // text when it said `natural` or said nothing. See core/TextDirection.h for
  // why that rule is answered there rather than asked of Pango, and
  // tests/test_text.cpp for the assertion that holds the two together.
  const bool rightToLeft = basalt::paragraphIsRightToLeft(direction, text);

  // Which edge, from core/TextAlignments.h: `start` and `end` are relative and
  // `left` and `right` are not, and all three hosts had been deciding that
  // locally and differently.
  const basalt::PhysicalTextAlignment physical =
      basalt::physicalTextAlignment(alignment, rightToLeft);
  const bool justify = physical == basalt::PhysicalTextAlignment::Justified;
  // Justified text stretches every line but the last, which goes against the
  // edge the paragraph starts from.
  basalt::PhysicalTextAlignment edge = justify
      ? (rightToLeft ? basalt::PhysicalTextAlignment::Right
                     : basalt::PhysicalTextAlignment::Left)
      : physical;

  // Pango's two physical alignments are only physical while `auto_dir` is off.
  // With it on -- which is what `natural` and no direction at all mean -- Pango
  // reads `PANGO_ALIGN_LEFT` and `_RIGHT` as the start and end of the
  // paragraph's own direction, so a physical answer has to be spelled backwards
  // for a right-to-left one. Measured on 2026-10-10: a Hebrew paragraph asking
  // for ALIGN_RIGHT drew against the *left* edge, which is how
  // `textAlign: 'right'` on Hebrew text came to be wrong here.
  if (autoDirection && rightToLeft) {
    if (edge == basalt::PhysicalTextAlignment::Left) {
      edge = basalt::PhysicalTextAlignment::Right;
    } else if (edge == basalt::PhysicalTextAlignment::Right) {
      edge = basalt::PhysicalTextAlignment::Left;
    }
  }

  pango_layout_set_alignment(layout, toPangoAlignment(edge));
  pango_layout_set_justify(layout, justify);

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
  // Clip, the first name in its enum, so every ordinary paragraph arrives here
  // asking to be clipped and this guard is what lets text wrap at all. It used
  // to say the default was Tail, which is wrong in the direction that matters:
  // Tail would be harmless here, and Clip is the one the whole screen depends
  // on this guard refusing.
  if (paragraphAttributes.maximumNumberOfLines > 0) {
    pango_layout_set_ellipsize(layout, toPangoEllipsize(paragraphAttributes.ellipsizeMode));
    // A negative height is Pango's way of expressing a line count, and Pango
    // acts on it only while it is ellipsizing. For 'clip', which asks for a cut
    // and no ellipsis, this therefore records the limit without enforcing it,
    // and the two readers of the layout enforce it between them: textLayoutSize
    // below reports a box only as tall as the lines that will show, and the
    // widget clips its painting to the same height.
    //
    // Setting it anyway, rather than carrying the count alongside the layout,
    // is what lets both of them read the limit off the layout they were handed.
    // The widget layer has no React Native in it and never sees a
    // ParagraphAttributes, so a layout that did not say how many lines it may
    // show would leave it with nothing to go on.
    pango_layout_set_height(layout, -paragraphAttributes.maximumNumberOfLines);
  } else {
    pango_layout_set_ellipsize(layout, PANGO_ELLIPSIZE_NONE);
    // Explicitly, because Pango's default height is -1 rather than 0, and -1 is
    // indistinguishable from the `numberOfLines={1}` a limit of one line would
    // set. `rn_pango_clip_height` reads the limit back off the layout and has
    // nothing else to go on, so a paragraph that never asked for a limit has to
    // say so in the one field that carries the answer.
    pango_layout_set_height(layout, 0);
  }

  return layout;
}

PangoAttrList *buildTextAttributes(const TextAttributes &textAttributes) {

  PangoAttrList *attributes = pango_attr_list_new();
  // G_MAXUINT is Pango's "to the end", so the list stays correct as the user
  // types and the string it covers grows.
  applyFragmentAttributes(attributes, textAttributes, 0, G_MAXUINT);
  return attributes;
}

std::vector<facebook::react::Rect> attachmentFrames(
    PangoLayout *layout,
    const facebook::react::AttributedString &attributedString) {
  std::vector<facebook::react::Rect> frames;
  if (layout == nullptr) {
    return frames;
  }


  std::size_t at = 0;
  for (const auto &fragment : attributedString.getFragments()) {
    const std::size_t start = at;
    // The transformed length, because that is what went into the layout: a
    // `textTransform` that changes the byte count -- ß to SS -- would otherwise
    // put every attachment after it at the wrong index.
    at += transformedFragmentText(fragment).size();
    if (!fragment.isAttachment()) {
      continue;
    }

    const auto &size = fragment.parentShadowView.layoutMetrics.frame.size;
    if (size.width <= 0 || size.height <= 0) {
      frames.push_back(facebook::react::Rect{});
      continue;
    }

    // Where the shape sits on the line. `index_to_pos` answers in Pango units
    // and returns the *logical* rect of the run, which for a shaped range is the
    // box the attribute asked for, so the height comes back as the child's.
    PangoRectangle position = {};
    pango_layout_index_to_pos(layout, static_cast<int>(start), &position);

    // A right-to-left run is reported with a negative width, growing leftwards
    // from x. Normalising keeps the origin the left edge, which is what a frame
    // means to everything above this.
    if (position.width < 0) {
      position.x += position.width;
      position.width = -position.width;
    }

    frames.push_back(facebook::react::Rect{
        .origin = {.x = static_cast<facebook::react::Float>(position.x) / PANGO_SCALE,
                   .y = static_cast<facebook::react::Float>(position.y) / PANGO_SCALE},
        // The size React Native measured, not the one Pango echoes back, so a
        // rounding difference in the shape cannot move the view a fraction of a
        // point away from the box its own layout used.
        .size = size,
    });
  }

  return frames;
}

float gtkTextScale(GtkSettings *settings) {
  if (settings == nullptr) {
    return 1.0F;
  }
  int dpi = -1;
  g_object_get(settings, "gtk-xft-dpi", &dpi, nullptr);
  if (dpi <= 0) {
    return 1.0F;
  }
  return static_cast<float>(dpi) / (96.0F * 1024.0F);
}

void textLayoutSize(PangoLayout *layout, float *outWidth, float *outHeight) {

  int width = 0;
  int height = 0;
  pango_layout_get_size(layout, &width, &height);
  *outWidth = fromPangoUnits(width);
  *outHeight = fromPangoUnits(height);

  // `numberOfLines` with `ellipsizeMode: 'clip'`: the lines over the limit are
  // still in the layout, so what Pango just reported is the height of the whole
  // text and Yoga would hand out a box tall enough to show all of it. Reporting
  // only the part that will be painted is half of what makes the limit real;
  // the other half is the widget clipping to this same number, which it gets
  // from this same function.
  //
  // The width is left as Pango gave it, measured across every line including
  // the hidden ones. A wrapped paragraph's lines are all about as wide as the
  // width it was given, so this only shows up on text that was not wrapped at
  // all -- an explicit newline, with a longer line below the cut -- and the
  // alternative is a union of line extents that has to get alignment offsets
  // right before it is any more accurate than this.
  float visibleHeight = 0.0F;
  if (rn_pango_clip_height(layout, &visibleHeight)) {
    *outHeight = visibleHeight;
  }
}

} // namespace basalt
