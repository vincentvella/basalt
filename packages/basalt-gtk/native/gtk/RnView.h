// basalt-core — GTK4 view layer
//
// Deliberately free of React Native headers: this file only knows about
// absolute frames and paint properties. The mounting manager translates
// Fabric's ShadowViewMutation stream into calls on this API, so the widget
// layer can be built and exercised without the RN C++ core present.

#pragma once

#include <gtk/gtk.h>
#include <pango/pango.h>

G_BEGIN_DECLS

// ---------------------------------------------------------------------------
// RnLayout — a GtkLayoutManager that does no layout of its own.
//
// Yoga has already computed absolute frames by the time a mutation reaches us,
// so this places each child at the rect the shadow tree assigned it. Measuring
// returns zero: RN is the sole source of truth for size.
// ---------------------------------------------------------------------------

#define RN_TYPE_LAYOUT (rn_layout_get_type())
G_DECLARE_FINAL_TYPE(RnLayout, rn_layout, RN, LAYOUT, GtkLayoutManager)

// ---------------------------------------------------------------------------
// RnView — the GTK peer of a Fabric <View>.
// ---------------------------------------------------------------------------

#define RN_TYPE_VIEW (rn_view_get_type())
G_DECLARE_FINAL_TYPE(RnView, rn_view, RN, VIEW, GtkWidget)

RnView *rn_view_new(int tag);

// As above, but with an accessible role.
//
// GTK4 has no per-instance role setter: a role is a construct-only property, or
// is set once per widget class. RnView is one class for every React Native
// view, so the role has to be chosen when the widget is made. That is possible
// because Fabric delivers a view's props with the Create mutation that makes
// it -- but it does mean accessibilityRole cannot change afterwards.
RnView *rn_view_new_with_role(int tag, GtkAccessibleRole role);

// Fabric tag, for debugging and hit-test attribution.
int rn_view_get_tag(RnView *self);

// Frame in parent coordinates, straight from LayoutMetrics::frame.
void rn_view_set_frame(RnView *self, float x, float y, float width, float height);
void rn_view_get_frame(RnView *self, graphene_rect_t *out);

// BaseViewProps::backgroundColor. Passing has_color = FALSE paints nothing.
void rn_view_set_background_color(RnView *self, gboolean has_color, const GdkRGBA *color);

// BaseViewProps::opacity.
void rn_view_set_opacity(RnView *self, double opacity);

// Corner radii, in the order top-left, top-right, bottom-right, bottom-left.
// Each is a width/height pair, because React Native's radii are elliptical.
// Pass NULL for square corners.
void rn_view_set_border_radii(RnView *self, const graphene_size_t radii[4]);

// Border widths and colours, in the order top, right, bottom, left -- which is
// the order GTK's border node wants and the order CSS names them in.
void rn_view_set_borders(RnView *self, const float widths[4], const GdkRGBA colors[4]);

// One CSS box shadow, in React Native's own terms: `BoxShadow` carries exactly
// these six fields and nothing is converted on the way here.
//
// `spread` grows the shadow's box before the blur, and `inset` puts the shadow
// inside the view instead of behind it, which is a different GSK node. The
// shadow follows the view's own corner radii, GSK taking the outline rather than
// a rectangle.
typedef struct {
  float dx;
  float dy;
  float blur;
  float spread;
  GdkRGBA color;
  gboolean inset;
} RnBoxShadow;

// One colour stop of a gradient: an offset from 0 to 1 along the gradient line
// and the colour there. The same layout as GskColorStop, deliberately, so the
// array goes straight to GSK without being copied field by field.
typedef struct {
  float offset;
  GdkRGBA color;
} RnGradientStop;

// Which kind of gradient a record is. CSS's `background-image` is one list that
// can hold both, and the first in it is the one on top, so they cannot be two
// lists here without losing the order between them.
typedef enum {
  RN_GRADIENT_LINEAR,
  RN_GRADIENT_RADIAL,
} RnGradientKind;

// One `backgroundImage` gradient, already resolved against this view's size: a
// linear one's two end points, or a radial one's centre and two radii, in this
// view's own pixels.
//
// Resolved rather than described, because the resolution is CSS's and is shared
// with the AppKit host in core/Gradients.h -- the angle or the ending shape, the
// box size and the stop fixup all go into these numbers and the offsets. This
// layer draws what it is given, which is also why it does not need to know an
// angle from a keyword or a corner from a side.
//
// The stops are borrowed for the duration of the call.
typedef struct {
  RnGradientKind kind;
  // Linear only: the ends of the gradient line.
  graphene_point_t start;
  graphene_point_t end;
  // Radial only: the centre of the ending shape and its two radii. A circle is
  // an ellipse whose radii are equal, which is what CSS makes of one.
  graphene_point_t center;
  float radius_x;
  float radius_y;
  // Where the image goes and how it tiles, from `backgroundSize`,
  // `backgroundPosition` and `backgroundRepeat`. Resolved in
  // core/BackgroundLayers.h: `area` is the rectangle the gradient itself fills,
  // in this view's coordinates, and `tile` is what it repeats in -- the same
  // rectangle for `repeat`, wider for `space`, and the whole painting area on an
  // axis that does not repeat.
  //
  // The gradient's own geometry above is resolved against `area`'s size and
  // offset to its origin, so this layer draws rather than positions.
  graphene_rect_t area;
  graphene_rect_t tile;
  gboolean repeats;
  const RnGradientStop *stops;
  int stop_count;
} RnGradient;

// `backgroundImage`. Replaces whatever was there; pass NULL or 0 for none.
//
// Painted above the background colour and below the content, which is where CSS
// puts a background image, and clipped to the view's rounded box as the
// background colour is. First in the list is on top, so they are painted back to
// front.
void rn_view_set_gradients(RnView *self, const RnGradient *gradients, int count);
// How many are set, for the tests and the tree dump.
int rn_view_get_gradient_count(RnView *self);

// `boxShadow`. Replaces whatever was there; pass NULL or 0 for none.
//
// Order is CSS's: the first shadow in the list is the one on top, so these are
// painted back to front. Outset shadows go behind the background and inset ones
// above it and below the content, which is where CSS puts them.
//
// The whole list rather than one shadow, because a card with a tight dark shadow
// and a wide soft one is the usual reason to ask, and because the two hosts that
// can do it both can do it for any number.
void rn_view_set_box_shadows(RnView *self, const RnBoxShadow *shadows, int count);
// How many are set, for the tests and the tree dump.
int rn_view_get_box_shadow_count(RnView *self);

// `filter`, already resolved: the colour matrix the list comes to, one blur
// radius and one opacity. See core/Filters.h, which is shared with the AppKit
// host and does the arithmetic the Filter Effects spec specifies.
//
// `matrix` is row-major in R, G, B, A order and `offset` is added after it, both
// in straight (unpremultiplied) colour, which is the space GSK's colour-matrix
// node works in. `blur_radius` is CSS's radius, twice the gaussian's sigma.
// `opacity` is 1 when the list has no `opacity()`.
// One `dropShadow()` from a filter list: a shadow of the subtree's alpha rather
// than of its box, which is what makes it a filter and not a box shadow.
//
// `radius` is CSS's blur radius, twice the gaussian's sigma, which is what
// `GskShadow` takes. React Native hands over a standard deviation, so the
// mounting manager doubles it; see core/Filters.h.
typedef struct {
  float dx;
  float dy;
  float radius;
  GdkRGBA color;
} RnFilterShadow;

typedef struct {
  gboolean has_matrix;
  float matrix[16];
  float offset[4];
  float blur_radius;
  float opacity;
  // Borrowed for the duration of the call, in the order they were written: the
  // first is the one nearest the content.
  const RnFilterShadow *shadows;
  int shadow_count;
} RnFilters;

// `filter`. Pass NULL for none.
//
// Applies to this view and everything inside it, which is what CSS does and what
// makes it a layer rather than a paint: the nodes wrap the whole subtree.
void rn_view_set_filters(RnView *self, const RnFilters *filters);

// `hitSlop`: how far outside its own box this view answers a press, in the order
// top, right, bottom, left -- the order CSS names edges and the order the border
// widths arrive in. NULL or all zeroes is no slop.
//
// Positive values grow the target. A 44pt row with a 20pt icon in it is the
// reason the prop exists: the icon is what the eye aims at and the cursor misses
// it.
//
// Implemented by widening GtkWidget's `contains`, which is what
// `gtk_widget_pick` asks, so it applies wherever picking does: a press, a hover
// and a drop all go through one answer. Nothing about drawing reads it.
void rn_view_set_hit_slop(RnView *self, const float insets[4]);

// `borderStyle`, for the two values that are not solid.
typedef enum {
  RN_BORDER_SOLID = 0,
  RN_BORDER_DOTTED,
  RN_BORDER_DASHED,
} RnBorderStyle;

// A dotted or dashed border is a different node from a solid one. GTK's border
// node paints solid only, so these are stroked: a path around the view's own
// rounded rectangle, with a dash pattern scaled to the border width the way
// every browser does it, since a fixed pattern looks like a hairline on a thick
// border and like a solid line on a thin one.
//
// One style for the whole outline rather than one per side, which is the limit
// of this: a stroked path has one dash pattern, and React Native's per-side
// `borderStyles` would need four paths and four strokes with the corners divided
// between them. A border with different styles per side is rare enough that the
// first style set wins, and the backlog records that.
void rn_view_set_border_style(RnView *self, RnBorderStyle style);

// `outlineWidth`, `outlineColor`, `outlineOffset` and `outlineStyle`.
//
// CSS's outline, which is not a border: it is drawn *outside* the box, takes no
// layout space, and sits `offset` away from the border edge. A focus ring is what
// it is for, and web-ported code sets it expecting exactly that.
//
// The radii follow the view's own, grown by the width and the offset so the ring
// stays concentric with a rounded card. A zero width is no outline; the style is
// the same three values a border has.
void rn_view_set_outline(RnView *self,
                         float width,
                         float offset,
                         const GdkRGBA *color,
                         RnBorderStyle style);

// BaseViewProps::transform, already resolved by React Native.
//
// Applied in the layout manager rather than at paint time, so that GTK's own
// hit testing follows it: a transformed view is picked where it appears, not
// where its frame says it is. Anchored on the view's centre, which is what
// every other React Native platform does and what transformOrigin is measured
// against. Pass NULL for identity.
void rn_view_set_transform(RnView *self, const graphene_matrix_t *matrix);

// `backfaceVisibility: 'hidden'`. A view whose transform has turned it away
// from the viewer is not drawn, and nor are its children -- which is what a
// card's back should do, and what takes it out of hit testing too, since
// `gtk_widget_pick` skips a widget that is not visible.
//
// Whether it has turned away is `basalt::facesAway`; see core/Backface.h for
// why the rule is shared rather than left to each toolkit.
void rn_view_set_hides_back_face(RnView *self, gboolean hides);

// `display: 'none'`. Separate from the back face above because the two are
// independent reasons to hide the same widget, and Fabric applies them through
// different calls -- props, then layout metrics.
void rn_view_set_hidden(RnView *self, gboolean hidden);

// BaseViewProps::zIndex. GTK paints in child order, so this reorders painting
// without touching the child list that mutations index into.
void rn_view_set_z_index(RnView *self, int z_index);

// `pointerEvents`, which decides what a press can land on rather than what is
// drawn. CSS's four values, and React Native's.
//
// GTK expresses exactly one of them. `none` is `can-target`, which makes
// `gtk_widget_pick` skip the widget and everything inside it, so a press
// reaches whatever is behind -- which is precisely the semantics. The other two
// have no equivalent, so they are stored here and resolved by the hit test;
// see GtkTouchDispatcher.cpp.
typedef enum {
  RN_POINTER_EVENTS_AUTO,
  RN_POINTER_EVENTS_NONE,
  RN_POINTER_EVENTS_BOX_NONE,
  RN_POINTER_EVENTS_BOX_ONLY,
} RnPointerEvents;

void rn_view_set_pointer_events(RnView *self, RnPointerEvents mode);
RnPointerEvents rn_view_get_pointer_events(RnView *self);

// Whether this view takes keyboard focus, and therefore whether Tab stops on
// it.
//
// React Native has a `focusable` prop and it does not reach this platform:
// ReactCommon parses it only into Android's and tvOS's HostPlatformViewProps,
// and the C++ host's is a bare alias of BaseViewProps. What does reach here is
// `accessible`, which is what <Pressable> sets on everything it renders and
// what an app sets on anything else it means as a control -- so that is the
// signal, and it is also the one both desktops use for their own focus rings.
// See GtkFocus.h.
//
// GTK owns the chain: a focusable widget joins the window's focus order, so Tab
// and Shift+Tab work without this project deciding what "next" means.
void rn_view_set_focusable(RnView *self, gboolean focusable);
gboolean rn_view_get_focusable(RnView *self);

// The text of a <Paragraph>, already laid out.
//
// A Paragraph is a View that also paints text, so it gets no separate widget
// type: same box model, same frame, same children. The mounting manager builds
// the layout from the shadow view's AttributedString -- Pango is part of the
// GTK stack, so taking a PangoLayout here keeps this file free of React Native
// types the way the rest of it is.
//
// Takes its own reference. Pass NULL to clear. `color` is the colour to paint
// glyphs that carry no foreground attribute of their own.
void rn_view_set_text_layout(RnView *self, PangoLayout *layout, const GdkRGBA *color);

// How much of a laid-out paragraph its own `numberOfLines` leaves visible, in
// points.
//
// Returns TRUE when the layout holds more lines than it is allowed to show, so
// that something has to hide the rest, and writes the height to keep into
// `out_height`. Returns FALSE when every line it has is a line it may show, and
// leaves `out_height` alone.
//
// This exists because of the one gap between Pango's text model and React
// Native's. A line limit is expressed to Pango by setting a negative height,
// and Pango acts on that only while it is ellipsizing: with `ellipsizeMode` of
// head, middle or tail the surplus lines are already gone from the layout that
// arrives here, and with 'clip', which asks for a cut and no ellipsis, every
// line is still in it. So clip is the one mode whose truncation has to be
// carried out by whoever holds the layout, and there are two of those -- the
// measurement, which must report a box only as tall as the lines that will
// show, and the widget, which must paint no further than that box. Both read
// the answer from here so that the box and the ink inside it cannot disagree,
// which would be a worse bug than no truncation at all.
//
// It lives on this side of the GTK layer, among the widgets, because the
// widgets may not depend on React Native and PangoTextLayout.h does. The
// measuring half already links this library and can reach across to it; the
// other direction would drag Fabric into a target that deliberately builds
// without it.
gboolean rn_pango_clip_height(PangoLayout *layout, float *out_height);

// How an image fills its frame. Mirrors React Native's ImageResizeMode, minus
// Repeat, which needs a repeating pattern node rather than one texture draw.
typedef enum {
  RN_IMAGE_FIT_COVER,
  RN_IMAGE_FIT_CONTAIN,
  RN_IMAGE_FIT_STRETCH,
  RN_IMAGE_FIT_CENTER,
  // Tiled from the top left at the image's own size, which is what CSS
  // `repeat` and React Native's `resizeMode: 'repeat'` mean. Every toolkit
  // here has a primitive for it; centring, which is what this used to fall
  // back to, is a different picture rather than a rougher one.
  RN_IMAGE_FIT_REPEAT,
} RnImageFit;

// The decoded pixels of an <Image>. Takes its own reference; pass NULL to clear.
//
// Like the text layout above, this is a GTK type rather than a React Native
// one, so the widget layer stays free of RN headers. GtkImageLoader produces
// the texture and GtkMountingManager chooses the fit.
void rn_view_set_texture(RnView *self, GdkTexture *texture, RnImageFit fit);

// `tintColor`: recolours the image, keeping its alpha. An icon drawn as a
// silhouette is the usual reason -- one asset, any colour.
//
// Separate from the texture because it arrives from the props and the texture
// arrives from a loader callback, and either can land first.
void rn_view_set_image_tint(RnView *self, gboolean has_tint, const GdkRGBA *tint);

// Called on the GTK main thread when this widget's own allocation changes.
//
// The host attaches one to a surface root to drive
// ReactHost::setSurfaceConstraints. GTK4 removed GtkWidget::size-allocate, and
// a layout manager's allocate is the supported replacement: it is the one
// place a widget is told the size it actually got.
typedef void (*RnViewResizeFunc)(RnView *self, int width, int height, gpointer user_data);
void rn_view_set_resize_callback(RnView *self, RnViewResizeFunc callback, gpointer user_data);

// Clip children to this view's bounds.
//
// Off by default, because React Native's default is overflow: visible. A
// <ScrollView> needs it, and so does any view with overflow: 'hidden'.
void rn_view_set_clips_children(RnView *self, gboolean clips);

// Shift children by a scroll offset, in points.
//
// Applied in the layout manager rather than by moving frames, so the frames
// Fabric assigned stay untouched and GTK's own hit testing follows the shift
// for free: a child allocated at its scrolled position is picked there.
void rn_view_set_scroll_offset(RnView *self, double offset_x, double offset_y);

// The overlay scrollbars this view draws over its own content. Computed by the
// scroll manager from core/ScrollIndicator.h, because the geometry is the same
// on all three desktops and the drawing is not.
//
// Lengths of zero mean "no indicator on that axis", which is what content that
// fits produces.
void rn_view_set_scroll_indicators(RnView *self,
                                   double vertical_offset,
                                   double vertical_length,
                                   double horizontal_offset,
                                   double horizontal_length);
void rn_view_get_scroll_offset(RnView *self, double *offset_x, double *offset_y);

// A textual description of the widget tree rooted here, one indented line per
// view.
//
// This is what makes the full stack testable rather than merely watchable.
// Everything below GDK has unit tests, but the path from JavaScript through
// React, Fabric and the mounting manager can only be exercised by running the
// real host -- and until now the only way to check the result was to look at a
// screenshot. A dump can be asserted on.
//
// Returns a newly allocated string; free with g_free.
char *rn_view_describe_tree(RnView *self);

// Turns this view into a text field, or back.
//
// The editable is a real GtkText -- the widget behind GtkEntry -- parented
// inside this one and sized to its whole frame. Using GTK's own editable rather
// than drawing a cursor on a PangoLayout is what brings input methods,
// selection, the clipboard and every keybinding a Linux user expects, none of
// which is worth reimplementing.
//
// Returns the editable, or NULL after clearing.
GtkWidget *rn_view_set_editable(RnView *self, gboolean editable, gboolean multiline);

// The border and padding to hold the native peer inside. Yoga has already
// resolved these into React Native's content inset; without them a <TextInput>
// renders its text flush against its own border, ignoring paddingHorizontal.
void rn_view_set_peer_insets(RnView *self, const GtkBorder *insets);
void rn_view_get_peer_insets(RnView *self, GtkBorder *out);
GtkWidget *rn_view_get_editable(RnView *self);

// --- Controls ----------------------------------------------------------------
//
// The three components that are a toolkit control rather than a box:
// <ActivityIndicator> and <RefreshControl> are a GtkSpinner, <Switch> is a
// GtkSwitch. Parented and sized exactly the way the <TextInput> peer above is
// -- a non-RnView child fills the view's inner rect -- because it is the same
// arrangement for the same reason: GTK's own widget brings the theme, the
// animation and the accessibility with it, and none of that is worth drawing
// by hand.
typedef enum {
  RN_CONTROL_NONE,
  RN_CONTROL_SPINNER,
  RN_CONTROL_SWITCH,
} RnControlKind;

// Installs the control this view stands for, replacing any other kind, or
// removes it for RN_CONTROL_NONE. Returns the control widget, or NULL.
GtkWidget *rn_view_set_control(RnView *self, RnControlKind kind);
GtkWidget *rn_view_get_control(RnView *self);
RnControlKind rn_view_get_control_kind(RnView *self);

// What `describe_tree` prints for this control, which core/DesktopControls.h
// writes so that three hosts cannot describe the same switch differently. NULL
// or empty prints nothing.
void rn_view_set_control_description(RnView *self, const char *description);

// Whether this control is disabled. Kept on the view rather than read back off
// the widget because `sensitive` is GTK's word for two different things, and
// only React Native's `disabled` should stop a press.
void rn_view_set_control_disabled(RnView *self, gboolean disabled);
gboolean rn_view_get_control_disabled(RnView *self);

// --- React DevTools' overlay --------------------------------------------------
//
// The rectangles a `DebuggingOverlay` draws: an inspected element's blue box,
// or an outline around everything that just re-rendered. Set from
// GtkMountingManager, which parses them; see core/DebuggingOverlay.h.
//
// Eight floats per rectangle -- x, y, width, height, then r, g, b, a -- and a
// flag for whether to fill. A plain array rather than the core struct because
// this file has no React Native in it and is not about to start.
void rn_view_set_highlights(RnView *self,
                            const float *rectangles,
                            const gboolean *filled,
                            int count);

// Accessibility, as a screen reader sees it.
//
// `label` is the accessible name and `description` the hint; either may be NULL
// or empty to leave it unset. GTK maps these onto AT-SPI, which is what Orca
// reads.
void rn_view_set_accessible_text(RnView *self, const char *label, const char *description);

// React Native's own name for the role -- "button", "image", "text" -- kept
// alongside the GtkAccessibleRole the widget was constructed with.
//
// Two vocabularies for one fact, and both are needed. GTK's is what a screen
// reader reads; React Native's is what `rn_view_describe_tree` reports, so that
// dump can be compared line by line with the AppKit one without either platform
// speaking the other's language. That the GTK role was really applied is
// asserted in tests/test_accessibility.cpp instead.
//
// Set from the same computation that picks the GtkAccessibleRole, so the two
// cannot disagree. Pass NULL or "" for no role.
void rn_view_set_role_name(RnView *self, const char *name);

// The view's `nativeID`, verbatim.
//
// Only the hidden title bar reads it, to find the regions an app marked with
// <TitleBar.DragRegion> and <TitleBar.NoDragRegion>; see
// core/TitleBarRegions.h for the two names and the rule. Nothing about layout,
// painting or accessibility looks at it.
//
// Kept as the app set it rather than parsed into a flag here, because a view
// can carry a nativeID for reasons that have nothing to do with the title bar
// -- an end-to-end test looking a view up by name, most of them -- and this
// layer has no business deciding which of those meanings was intended.
//
// Pass NULL or "" for none.
void rn_view_set_native_id(RnView *self, const char *native_id);
// NULL when unset, never the empty string.
const char *rn_view_get_native_id(RnView *self);

// The `cursor` style property, as the CSS keyword React Native and GDK both use:
// "pointer", "text", "grab", "ns-resize" and the rest. NULL or "" leaves the
// cursor to whatever encloses this view, which is what `cursor: 'auto'` means.
//
// Straight through to `gtk_widget_set_cursor_from_name`, GDK's names being CSS's.
// A name no cursor theme has falls back to the default, which is GDK's business
// rather than this layer's -- the keyword is still stored and still reported, so
// the cross-host dump compares what the app asked for rather than what a theme
// happened to have.
void rn_view_set_cursor(RnView *self, const char *name);
// NULL when unset, never the empty string.
const char *rn_view_get_cursor(RnView *self);

// `textShadowColor`, `textShadowOffset` and `textShadowRadius`, resolved into one
// shadow for the paragraph by core/TextShadows.h.
//
// `radius` is CSS's blur radius, twice the gaussian's sigma, which is what
// `GskShadow` takes -- React Native hands over a standard deviation, so the
// mounting manager doubles it, as it does for a `dropShadow()` filter. A colour
// with no alpha, or a shadow with no offset and no blur, is none.
//
// Set with the layout rather than beside it, because a paragraph and its shadow
// change together: see rn_view_set_text_layout.
void rn_view_set_text_shadow(RnView *self,
                             float dx,
                             float dy,
                             float radius,
                             const GdkRGBA *color);

// `mixBlendMode`, as the CSS keyword: "multiply", "screen", "color-dodge" and
// the rest, which is what core/BlendModes.h hands over. NULL or "" is `normal`,
// meaning ordinary source-over compositing.
//
// The view does not paint its own blend -- a blend needs the backdrop, which is
// its parent's business -- so this is read by the parent's snapshot, and setting
// it queues a redraw on the parent. GSK has a mode for every CSS mode bar
// `plus-lighter`, which is stored and reported but paints unblended.
void rn_view_set_blend_mode(RnView *self, const char *name);

// The keyword that was set, or NULL. What the app asked for rather than what GSK
// could do with it, which is the same rule the cursor name follows.
const char *rn_view_get_blend_mode(RnView *self);

// `accessibilityLabelledBy`, already resolved: the views whose text names this
// one. Pass NULL or 0 to take the relation off again.
//
// The prop names other views by their `nativeID` and the resolution is shared
// with the AppKit host in core/LabelRegistry.h, which also decides *when* --
// Fabric mounts in tree order, so a field labelled by the text after it is
// mounted before its label exists. This layer is handed the answer.
//
// GTK models it as an accessible relation, which is exactly what it is: AT-SPI
// gets a list of references rather than a copied string, so a label that changes
// its text does not leave a stale copy behind.
void rn_view_set_labelled_by(RnView *self, RnView **labels, int count);

// The text of this view and everything inside it, as a screen reader would read
// it: each paragraph's string in tree order, separated by single spaces.
//
// Here for `accessibilityLiveRegion`, which announces a status message when it
// changes and so needs to know what the message currently says. An accessible
// label stands in for the text where one was set, which the mounting manager
// decides before calling this -- GTK has no way to read a label back. Returns a
// string to free with g_free, never NULL.
char *rn_view_collect_text(RnView *self);

// Reads `text` out to assistive technology now, as `accessibilityLiveRegion`
// asks. Not a property: GTK announces at a moment, and whether anybody is
// listening is the screen reader's business.
//
// `assertive` interrupts what is being read; polite waits for a gap, which is
// what the two values of the prop mean.
//
// The announcement is also remembered, which is the only way to see that it
// happened: nothing in an automated run is connected to AT-SPI, so
// `rn_view_get_last_announcement` is what the tests and the end-to-end suite
// read. It is deliberately not in the tree dump -- an announcement is an event
// and the dump is state -- and the host logs it as well.
void rn_view_announce(RnView *self, const char *text, gboolean assertive);
// The last text this view announced, or NULL. Owned by the view.
const char *rn_view_get_last_announcement(RnView *self);

// Accessible states. Each is a tri-state: unset leaves GTK's default alone,
// which is not the same as setting it false.
typedef enum {
  RN_A11Y_UNSET,
  RN_A11Y_FALSE,
  RN_A11Y_TRUE,
} RnAccessibleFlag;

// `accessibilityValue`, which React Native models as four independent optionals:
// a numeric range (min, max, now) and a text form that overrides how the number
// is read out. GTK has a property for each, so each one that is set is set and
// each one that is not is reset rather than defaulted: a view that says nothing
// about its range is not a slider at zero, and announcing it as one would be
// worse than announcing nothing.
//
// RN_A11Y_VALUE_UNSET for an absent number. Zero cannot stand in for absent,
// `now: 0` being a perfectly ordinary value at the bottom of a range.
#define RN_A11Y_VALUE_UNSET G_MININT

void rn_view_set_accessible_value(RnView *self, int min, int max, int now, const char *text);

// What a screen reader should call this view, for the case where its
// `accessibilityRole` changed after it was mounted.
//
// GTK's `accessible-role` is construct-only, so the role a widget was created
// with is the role it dies with, and an app that swaps a view's role mid-life
// used to be ignored outright. The role description is the one part of that
// which can still be updated, and it is what a screen reader announces, so the
// announcement follows even though the underlying role does not. That is a
// partial answer and the limits are real: anything a screen reader infers from
// the role itself, such as which navigation commands apply, still follows the
// original. See backlog/accessibility.md.
//
// Null or empty clears it, which is what a view whose role never changed wants:
// a description identical to its own role would be noise.
void rn_view_set_accessible_role_description(RnView *self, const char *description);

// `blurRadius` on an `<Image>`, in points. Zero or less is no blur, which is
// also the default React Native sends, so an ordinary image pays nothing.
//
// Applied to the image alone rather than to the view, which is what the prop
// means: a blurred photograph behind sharp text is the usual reason to ask for
// it, and blurring the whole view would take the text with it. It therefore sits
// inside the clip and the tiling, so a blurred `cover` image is still cut to its
// box and a blurred `repeat` blurs each tile the same way.
void rn_view_set_image_blur(RnView *self, float radius);

void rn_view_set_accessible_state(RnView *self,
                                  RnAccessibleFlag disabled,
                                  RnAccessibleFlag checked,
                                  RnAccessibleFlag selected,
                                  RnAccessibleFlag expanded,
                                  RnAccessibleFlag busy);

// Hidden from assistive technology, for accessible={false} and
// accessibilityElementsHidden.
void rn_view_set_accessible_hidden(RnView *self, gboolean hidden);

// Child management. Mirrors Insert/Remove mutations; index is the position
// within the parent's child list, as Fabric numbers it.
void rn_view_insert_child(RnView *self, RnView *child, int index);
void rn_view_remove_child(RnView *self, RnView *child);

G_END_DECLS
