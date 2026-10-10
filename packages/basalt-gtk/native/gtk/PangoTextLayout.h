// Turning React Native's AttributedString into a PangoLayout.
//
// This is the one place that knows both halves, and it is deliberately used by
// both sides of the text problem:
//
//   - `TextLayoutManager::measure` (src/PangoTextLayoutManager.cpp) builds a
//     layout to ask Pango how big the text is, so Yoga can lay it out.
//   - `GtkMountingManager` builds the same layout to hand to the widget, which
//     paints it in `snapshot`.
//
// Measuring and painting must agree, so they must not build layouts differently.
// Sharing this function is what guarantees that.

#pragma once

#include <gtk/gtk.h>
#include <pango/pangocairo.h>

#include "FontFitting.h"

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>
#include <react/renderer/graphics/Rect.h>

#include <vector>

namespace basalt {

// Builds a PangoLayout for `attributedString`.
//
// `maxWidth` is in React Native's logical points; pass a negative value for
// unconstrained width. There is no scale-factor argument: every size here is in
// that same logical space, and GTK applies the display scale when it renders.
//
// Returns a new reference; the caller owns it and must g_object_unref it.
//
// Thread-safe. Fabric measures text off the main thread while GTK paints on it,
// so this serialises internally rather than assuming Pango's font map is
// reentrant.
// `fit` is `adjustsFontSizeToFit`'s answer: the ratio every font size is
// multiplied by and the two bounds that ratio is clamped against. The default
// is the one a paragraph that never asked gets, which changes nothing -- see
// core/FontFitting.h.
PangoLayout *buildTextLayout(const facebook::react::AttributedString &attributedString,
                             const facebook::react::ParagraphAttributes &paragraphAttributes,
                             float maxWidth,
                             basalt::FontFit fit = {});

// How far this paragraph's fonts have to shrink to fit a box, for
// `adjustsFontSizeToFit`.
//
// The search is core's; this is the half that measures, which means building a
// layout per probe -- about eight of them -- and is why it runs only for a
// paragraph that asked. Both the measurement seam and the mounting manager call
// it, with the constraints and the frame respectively, so the text is painted
// at the size it was measured at.
basalt::FontFit textFitScale(const facebook::react::AttributedString &attributedString,
                             const facebook::react::ParagraphAttributes &paragraphAttributes,
                             float maxWidth,
                             float maxHeight);

// Builds a PangoAttrList applying `textAttributes` to a whole string, for the
// widgets that hold their own text rather than a layout we built: GtkText takes
// a PangoAttrList through gtk_text_set_attributes, which is the only way a
// <TextInput> can honour `color`, `fontSize` and `fontFamily` from its style.
// Without it the field renders in GTK's theme colour, which on a dark
// background is dark text on dark.
//
// Returns a new reference; the caller owns it and must pango_attr_list_unref
// it. Thread-safe on the same terms as buildTextLayout.
PangoAttrList *buildTextAttributes(const facebook::react::TextAttributes &textAttributes);

// The desktop's text scale, as GTK publishes it.
//
// GNOME's "Large Text", and the `text-scaling-factor` behind it, arrive as a
// font *resolution* rather than as a scale: `GtkSettings:gtk-xft-dpi` is
// 1024ths of a dot per inch, 96 being unscaled, so a scaling factor of 1.25
// reads as 120 * 1024. A display that said nothing reads as -1, which is 1.
//
// Main thread only, which is why the host reads it once at startup and hands it
// to core/FontScaling.h rather than the text builders asking: this function
// touches GtkSettings, and text is measured off the main thread. Takes the
// settings object rather than fetching it, so the host's `notify::gtk-xft-dpi`
// handler can pass the one it was given.
float gtkTextScale(GtkSettings *settings);

// The size of a laid-out paragraph, in points. Pango reports 1/1024ths of a
// pixel, and forgetting to divide by PANGO_SCALE is the classic bug here.
void textLayoutSize(PangoLayout *layout, float *outWidth, float *outHeight);

// Where each inline view ended up, in paragraph coordinates and in points, one
// entry per attachment fragment and in fragment order, which is the order
// `ParagraphShadowNode` pairs them back up in.
//
// Must be given the same `attributedString` the layout was built from: the byte
// offsets are recomputed by walking the fragments the same way, because Pango
// indexes by byte into the concatenated string and nothing else here remembers
// where a fragment started.
//
// A fragment whose child measured to nothing gets a zero rect, which is what it
// had before any of this and the only honest answer when no size was given.
std::vector<facebook::react::Rect> attachmentFrames(
    PangoLayout *layout,
    const facebook::react::AttributedString &attributedString);

} // namespace basalt
