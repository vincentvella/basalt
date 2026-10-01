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

#include <pango/pangocairo.h>

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/ParagraphAttributes.h>
#include <react/renderer/attributedstring/TextAttributes.h>

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
PangoLayout *buildTextLayout(const facebook::react::AttributedString &attributedString,
                             const facebook::react::ParagraphAttributes &paragraphAttributes,
                             float maxWidth);

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

// The size of a laid-out paragraph, in points. Pango reports 1/1024ths of a
// pixel, and forgetting to divide by PANGO_SCALE is the classic bug here.
void textLayoutSize(PangoLayout *layout, float *outWidth, float *outHeight);

} // namespace basalt
