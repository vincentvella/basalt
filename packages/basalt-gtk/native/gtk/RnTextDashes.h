// A dotted or dashed underline, which Pango has no value for.
//
// `pango_attr_underline_new` takes an enum -- single, double, and the wavy one
// a spell checker draws -- with no patterns in it, so a dotted line asked of
// Pango comes out solid. Rather than approximate it, `PangoTextLayout.cpp`
// leaves the attribute off that range and records the run here, and `RnView`
// draws the pieces in its snapshot, where it already has the layout, its lines
// and a way to append a rectangle.
//
// The runs travel *on the layout*, under a GObject data key, rather than beside
// it. Every caller of `buildTextLayout` would otherwise have to carry them, and
// the code that paints is three calls away from the code that builds; attaching
// them to the layout is GObject's own answer to that, and it keeps a dash from
// ever being separated from the text it belongs under.
//
// This header is deliberately free of React Native: `RnView` is in
// `basalt_gtk_view`, which builds without it, so the record cannot live in
// `PangoTextLayout.h` where the rest of the layout vocabulary is.

#pragma once

#include <gtk/gtk.h>
#include <pango/pango.h>

// One run of text wanting a patterned underline, in the layout's own byte
// offsets -- the same unit Pango's attribute indices and line ranges use.
//
// `dotted` separates the two patterns, there being only two: dotted is a
// square per thickness, dashed is a longer piece with a shorter gap. `color`
// is the `textDecorationColor` the app named, and `has_color` false means the
// text's own colour, which is what both toolkits do when none is given.
struct RnTextDashRun {
  guint start;
  guint end;
  gboolean dotted;
  gboolean has_color;
  GdkRGBA color;
};

// The key the runs hang on. Named rather than spelled twice, since a typo in
// one of the two would be a silent "this paragraph has no dashes".
#define RN_TEXT_DASH_RUNS_KEY "rn-text-dash-runs"

// Takes ownership of `runs`, which must be a GArray of RnTextDashRun. Passing
// an empty array frees it and attaches nothing, so a caller can build one
// unconditionally and hand it over.
static inline void rn_text_set_dash_runs(PangoLayout *layout, GArray *runs) {
  if (layout == nullptr || runs == nullptr) {
    return;
  }
  if (runs->len == 0) {
    g_array_unref(runs);
    return;
  }
  g_object_set_data_full(G_OBJECT(layout), RN_TEXT_DASH_RUNS_KEY, runs,
                         reinterpret_cast<GDestroyNotify>(g_array_unref));
}

// The runs on a layout, or NULL for one with none. Owned by the layout.
static inline GArray *rn_text_dash_runs(PangoLayout *layout) {
  if (layout == nullptr) {
    return nullptr;
  }
  return static_cast<GArray *>(g_object_get_data(G_OBJECT(layout), RN_TEXT_DASH_RUNS_KEY));
}
