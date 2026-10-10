#include "RnView.h"

#include "Backface.h"
#include "ScrollIndicator.h"
#include "ImageAnimation.h"

#include "ControlMetrics.h"
#include "FocusRing.h"
#include "GtkTextPeer.h"

#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// RnLayout
// ---------------------------------------------------------------------------

struct _RnLayout {
  GtkLayoutManager parent_instance;
};

G_DEFINE_TYPE(RnLayout, rn_layout, GTK_TYPE_LAYOUT_MANAGER)

static void rn_layout_measure(GtkLayoutManager * /*manager*/,
                              GtkWidget * /*widget*/,
                              GtkOrientation /*orientation*/,
                              int /*for_size*/,
                              int *minimum,
                              int *natural,
                              int *minimum_baseline,
                              int *natural_baseline) {
  // RN owns sizing. Reporting zero keeps GTK from ever second-guessing Yoga.
  *minimum = 0;
  *natural = 0;
  *minimum_baseline = -1;
  *natural_baseline = -1;
}

// Defined below; lets allocate report the root's size without exposing the
// struct.
static void rn_view_notify_allocation(RnView *self, int width, int height);
static void rn_view_scroll_offset(RnView *self, double *offset_x, double *offset_y);
// Defined below, for the same reason: allocate runs before the struct is
// complete, so it reaches the fields through accessors.
static gboolean rn_view_layout_transform(RnView *self, graphene_matrix_t *out);
static int rn_view_layout_z_index(RnView *self);

static void rn_layout_allocate(GtkLayoutManager * /*manager*/,
                               GtkWidget *widget,
                               int width,
                               int height,
                               int /*baseline*/) {
  // A scrolling view moves its children rather than its own frame. Reading the
  // offset here, once, keeps it out of every child's stored frame.
  double scroll_x = 0.0;
  double scroll_y = 0.0;
  if (RN_IS_VIEW(widget)) {
    rn_view_scroll_offset(RN_VIEW(widget), &scroll_x, &scroll_y);
  }

  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (!gtk_widget_should_layout(child)) {
      continue;
    }

    // A native peer -- the GtkText inside a <TextInput> -- is not an RnView and
    // has no frame of its own. It fills the view that owns it.
    if (!RN_IS_VIEW(child)) {
      GtkBorder insets = {0, 0, 0, 0};
      if (RN_IS_VIEW(widget)) {
        rn_view_get_peer_insets(RN_VIEW(widget), &insets);
      }
      // Clamped: a field narrower than its own padding would otherwise be
      // allocated a negative size, which GTK treats as an error.
      const int inner_width = MAX(width - insets.left - insets.right, 0);
      const int inner_height = MAX(height - insets.top - insets.bottom, 0);

      int peer_width = inner_width;
      int peer_height = inner_height;
      int peer_x = insets.left;
      int peer_y = insets.top;

      // A control keeps its own shape and is centred; only the text peer
      // fills. A GtkSwitch stretched to whatever box the app gave it is a
      // rectangle with a circle in it, and a GtkSpinner stretched is an
      // ellipse -- neither of which any other React Native platform draws.
      // `halign` would say this declaratively, and does nothing here: this
      // layout manager allocates children directly rather than measuring them.
      if (RN_IS_VIEW(widget) && rn_view_get_control(RN_VIEW(widget)) == child) {
        int natural_width = 0;
        int natural_height = 0;
        gtk_widget_measure(child, GTK_ORIENTATION_HORIZONTAL, -1, nullptr, &natural_width, nullptr, nullptr);
        gtk_widget_measure(child, GTK_ORIENTATION_VERTICAL, natural_width, nullptr, &natural_height, nullptr, nullptr);
        peer_width = MIN(natural_width, inner_width);
        peer_height = MIN(natural_height, inner_height);
        peer_x += (inner_width - peer_width) / 2;
        peer_y += (inner_height - peer_height) / 2;
      }

      graphene_point_t offset;
      offset.x = static_cast<float>(peer_x);
      offset.y = static_cast<float>(peer_y);
      GskTransform *peer_transform = gsk_transform_translate(nullptr, &offset);
      gtk_widget_allocate(child, peer_width, peer_height, -1, peer_transform);
      continue;
    }

    graphene_rect_t frame;
    rn_view_get_frame(RN_VIEW(child), &frame);

    graphene_point_t origin;
    origin.x = frame.origin.x - static_cast<float>(scroll_x);
    origin.y = frame.origin.y - static_cast<float>(scroll_y);

    GskTransform *transform = gsk_transform_translate(nullptr, &origin);

    // A transform is anchored on the view's centre, which is where every other
    // React Native platform anchors it and what transformOrigin is measured
    // from. Composing it here rather than in snapshot() is what makes
    // gtk_widget_pick follow it, so a transformed view is hit where it looks.
    graphene_matrix_t matrix;
    if (rn_view_layout_transform(RN_VIEW(child), &matrix)) {
      graphene_point_t centre;
      centre.x = frame.size.width / 2.0f;
      centre.y = frame.size.height / 2.0f;
      graphene_point_t back;
      back.x = -centre.x;
      back.y = -centre.y;

      transform = gsk_transform_translate(transform, &centre);
      transform = gsk_transform_matrix(transform, &matrix);
      transform = gsk_transform_translate(transform, &back);
    }

    gtk_widget_allocate(child,
                        static_cast<int>(frame.size.width),
                        static_cast<int>(frame.size.height),
                        -1,
                        transform);
  }

  // Children are placed from frames RN already decided, so this is not part of
  // laying out; it is how the widget tells the host what size the window gave
  // it. On a surface root that becomes the next layout constraint.
  if (RN_IS_VIEW(widget)) {
    rn_view_notify_allocation(RN_VIEW(widget), width, height);
  }
}

static void rn_layout_class_init(RnLayoutClass *klass) {
  GtkLayoutManagerClass *layout_class = GTK_LAYOUT_MANAGER_CLASS(klass);
  layout_class->measure = rn_layout_measure;
  layout_class->allocate = rn_layout_allocate;
}

static void rn_layout_init(RnLayout * /*self*/) {}

// ---------------------------------------------------------------------------
// RnView
// ---------------------------------------------------------------------------

struct _RnView {
  GtkWidget parent_instance;

  int tag;
  graphene_rect_t frame;

  gboolean has_background_color;
  GdkRGBA background_color;
  double opacity;

  PangoLayout *text_layout;
  GdkRGBA text_color;

  GdkTexture *texture;
  RnImageFit texture_fit;
  // An animated image, which gdk-pixbuf owns the frames of: there is no
  // indexed access to them, only an iterator with a clock. The pacing is ours
  // anyway, through core/ImageAnimation.h's clamp and an elapsed time the
  // caller supplies, which is what lets a test move a GIF on without waiting
  // for it. See rn_view_set_animation.
  GdkPixbufAnimation *animation;
  GdkPixbufAnimationIter *animation_iter;
  // Where the iterator's own clock has got to, in microseconds since a zero
  // start. Its delays decide the frame boundaries; ours decide when.
  gint64 animation_at_us;
  // What is left over of the time advanced so far, against the current frame's
  // clamped delay.
  double animation_budget_ms;
  // The frame clock's timestamp at the last tick, so the advance is by the time
  // that actually passed rather than by an assumed refresh rate.
  gint64 animation_tick_at_us;
  guint animation_tick;
  double indicator_v_offset;
  double indicator_v_length;
  double indicator_h_offset;
  double indicator_h_length;
  // `indicatorStyle`, as the colour core resolved it to. Black and translucent
  // until something says otherwise, which is what every list that never set the
  // prop gets.
  GdkRGBA indicator_colour;
  // `caretHidden` and `contextMenuHidden`, for the dump. What the peer does
  // about them is in GtkTextPeer.cpp.
  gboolean caret_hidden;
  gboolean context_menu_hidden;
  gboolean has_image_tint;
  float image_blur;
  GdkRGBA image_tint;
  char *role_name;
  // `testID`, which is an identifier for a test runner rather than anything a
  // person sees. GTK publishes it as the accessible id; see
  // rn_view_accessible_init.
  char *test_id;
  // `accessibilityViewIsModal`, kept so the dump can report it: the property
  // itself goes straight to GTK and cannot be read back without an AT.
  gboolean accessible_modal;
  // `spellCheck` and `autoCorrect`, as React Native's own words: "on", "off" or
  // absent. Absent is the third state and not a default: see core/TextChecking.h.
  const char *spell_check;
  const char *auto_correct;
  // `autoCapitalize` and `keyboardType`, as React Native's own words. Always
  // present for a field: both props are plain enums with a default rather than
  // optionals, so there is no third state to leave out.
  const char *auto_capitalize;
  const char *keyboard_type;
  // `writingDirection`, as React Native's own word: "ltr", "rtl" or "natural".
  // The layout has the direction itself; this is for the dump, which is
  // compared with the AppKit one line by line.
  const char *writing_direction;
  // The `cursor` style property's CSS keyword, or NULL. Kept as well as handed
  // to GDK so the tree dump can report what the app asked for.
  char *cursor_name;
  // The app's `nativeID`. Read only by the hidden title bar's drag-region hit
  // test; see RnView.h and core/TitleBarRegions.h.
  char *native_id;

  gboolean clips_children;
  double scroll_x;
  double scroll_y;

  graphene_size_t border_radii[4];
  gboolean has_border_radii;
  float border_widths[4];
  // `mixBlendMode`, as the keyword the app asked for and the GSK mode it came
  // to. The two are separate because GSK has no plus-lighter: the keyword is
  // still reported, so the dump says what the app asked for rather than what
  // this compositor could do with it.
  // The `dropShadow()` functions from `filter`, owned: RnFilters borrows them
  // from the caller and this widget outlives the call. NULL for none.
  GArray *filter_shadows;

  char *blend_name;
  GskBlendMode blend_mode;
  gboolean blends;

  // The paragraph's text shadow, from core/TextShadows.h. `radius` is CSS's,
  // which is what GSK takes. A zero alpha is no shadow.
  float text_shadow_dx;
  float text_shadow_dy;
  float text_shadow_radius;
  GdkRGBA text_shadow_color;

  // CSS's outline: drawn outside the box, no layout space. Zero width is none.
  float outline_width;
  float outline_offset;
  GdkRGBA outline_color;
  RnBorderStyle outline_style;
  // The resolved `filter`, and whether there is one at all.
  RnFilters filters;
  gboolean has_filters;
  // `hitSlop`, top, right, bottom, left. Read only by rn_view_contains.
  float hit_slop[4];
  gboolean has_hit_slop;
  RnBorderStyle border_style;
  // The `boxShadow` list, in the order the app wrote it. NULL for none rather
  // than an empty array, so a view with no shadow allocates nothing.
  GArray *box_shadows;
  // The `backgroundImage` gradients, each owning its own stops. NULL for none.
  GArray *gradients;
  // The tags of the views in this one's LABELLED_BY relation, for the tree dump:
  // GTK holds the references itself and will not say what they are.
  GArray *labelled_by;
  // The resolved `experimental_accessibilityOrder`, by tag, for the dump.
  GArray *accessibility_order;
  // The last text this view announced as a live region. See rn_view_announce.
  char *last_announcement;
  GdkRGBA border_colors[4];
  gboolean has_borders;

  graphene_matrix_t transform;
  gboolean has_transform;
  gboolean hides_back_face;
  gboolean hidden_by_app;
  gboolean hidden_by_back_face;

  int z_index;

  RnPointerEvents pointer_events;
  gboolean focusable;

  // Border plus padding, for the native peer below. React Native calls this a
  // content inset and Yoga has already resolved it; a GtkText knows nothing
  // about either and would otherwise sit flush against the border.
  GtkBorder peer_insets;

  // The <TextInput> peer when this view is one, otherwise NULL: a GtkText for
  // a single-line field and a GtkTextView for a multiline one. Borrowed -- the
  // widget owns it once parented. See GtkTextPeer.h for why it is a GtkWidget
  // rather than either concrete type.
  GtkWidget *editable;

  // The toolkit control this view stands for -- a GtkSpinner or a GtkSwitch --
  // or NULL. Parented and allocated exactly like `editable` above, and never
  // at the same time as one: a <TextInput> is not a <Switch>.
  GtkWidget *control;
  RnControlKind control_kind;
  gboolean control_disabled;
  // What the tree dump prints for it, written by core/DesktopControls.h.
  char *control_description;

  // React DevTools' overlay rectangles: eight floats each, plus a fill flag.
  // See rn_view_set_highlights.
  GArray *highlights;
  GArray *highlight_filled;

  RnViewResizeFunc resize_callback;
  gpointer resize_data;
  int allocated_width;
  int allocated_height;
};

// One gradient as the widget keeps it: the two points, and its own stops. The
// stops are a GArray per gradient rather than one array for all of them, because
// a transition hint turns three stops into eleven and the lists are different
// lengths for no reason the caller controls.
struct RnGradientRecord {
  RnGradientKind kind;
  // Linear: the ends of the gradient line. Radial: the centre and the radii.
  graphene_point_t start;
  graphene_point_t end;
  graphene_point_t center;
  float radius_x;
  float radius_y;
  // Where the image goes and the tile it repeats in. See RnGradient.
  graphene_rect_t area;
  graphene_rect_t tile;
  gboolean repeats;
  GArray *stops;
};

// The `at=` and `tile=` half of a gradient's line, which both hosts spell the
// same way. Separate because the linear and radial branches share it.
static void rn_view_describe_gradient_layer(GString *out, const RnGradientRecord &gradient) {
  g_string_append_printf(out,
                         ",at=(%g,%g %gx%g)",
                         static_cast<double>(gradient.area.origin.x),
                         static_cast<double>(gradient.area.origin.y),
                         static_cast<double>(gradient.area.size.width),
                         static_cast<double>(gradient.area.size.height));
  if (gradient.repeats) {
    g_string_append_printf(out,
                           ",tile=(%g,%g %gx%g)",
                           static_cast<double>(gradient.tile.origin.x),
                           static_cast<double>(gradient.tile.origin.y),
                           static_cast<double>(gradient.tile.size.width),
                           static_cast<double>(gradient.tile.size.height));
  }
  g_string_append(out, ")");
}

static void rn_gradient_record_clear(gpointer data) {
  auto *record = static_cast<RnGradientRecord *>(data);
  g_clear_pointer(&record->stops, g_array_unref);
}


static void rn_view_notify_allocation(RnView *self, int width, int height) {
  if (self->resize_callback == nullptr) {
    return;
  }
  // allocate runs on every layout pass, most of which change nothing. Only a
  // real size change is worth a Fabric commit.
  if (width == self->allocated_width && height == self->allocated_height) {
    return;
  }
  self->allocated_width = width;
  self->allocated_height = height;
  self->resize_callback(self, width, height, self->resize_data);
}

void rn_view_set_resize_callback(RnView *self, RnViewResizeFunc callback, gpointer user_data) {
  g_return_if_fail(RN_IS_VIEW(self));
  self->resize_callback = callback;
  self->resize_data = user_data;
}

#if GTK_CHECK_VERSION(4, 22, 0)
// `testID` reaches AT-SPI as the accessible id, and the only way to say what it
// is is to answer for it: `GtkAccessibleIface::get_accessible_id` is a vfunc and
// GTK has no setter. GtkWidget implements GtkAccessible already, and a subclass
// re-implementing the interface overrides only the vfuncs it sets, which is what
// this does.
//
// Measured before it was written: `gtk_widget_set_name` does *not* feed the
// accessible id, and a widget built in code has no buildable id either, so both
// of the obvious ways report null.
//
// 4.22 is when the vfunc arrived. Before that GTK has nowhere to put this at
// all, so the whole thing is compiled out and backlog/accessibility.md records
// the version: Ubuntu 24.04, which CI runs, has 4.14.
static void rn_view_accessible_init(GtkAccessibleInterface *iface);

G_DEFINE_TYPE_WITH_CODE(RnView, rn_view, GTK_TYPE_WIDGET,
                        G_IMPLEMENT_INTERFACE(GTK_TYPE_ACCESSIBLE, rn_view_accessible_init))

static char *rn_view_get_accessible_id(GtkAccessible *accessible) {
  RnView *self = RN_VIEW(accessible);
  return self->test_id != nullptr ? g_strdup(self->test_id) : nullptr;
}

static void rn_view_accessible_init(GtkAccessibleInterface *iface) {
  iface->get_accessible_id = rn_view_get_accessible_id;
}
#else
G_DEFINE_TYPE(RnView, rn_view, GTK_TYPE_WIDGET)
#endif

static void rn_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  RnView *self = RN_VIEW(widget);

  const int width = gtk_widget_get_width(widget);
  const int height = gtk_widget_get_height(widget);

  // `filter`'s own opacity() multiplies the view's: CSS has two ways to ask for
  // the same thing and an app can use both.
  const double filter_opacity = self->has_filters ? self->filters.opacity : 1.0;
  const double effective_opacity = self->opacity * filter_opacity;
  const gboolean needs_opacity_layer = effective_opacity < 1.0;
  if (needs_opacity_layer) {
    gtk_snapshot_push_opacity(snapshot, effective_opacity);
  }

  // The rest of `filter`, wrapping this view and everything inside it, which is
  // what CSS applies it to. The blur is outside the colour matrix for no reason
  // that shows: both are linear, so a blurred-then-recoloured image and a
  // recoloured-then-blurred one are the same picture, which is also why
  // core/Filters.h is allowed to collapse the list into one of each.
  const gboolean filter_blurs = self->has_filters && self->filters.blur_radius > 0.0f;
  const gboolean filter_recolours = self->has_filters && self->filters.has_matrix;

  // `dropShadow()`, which is a shadow of this subtree's *alpha* rather than of
  // its box: that is the whole difference from `boxShadow`, and GSK has the node
  // for it. Outermost of the filter effects, so the shadow is cast by the
  // blurred and recoloured picture and is not itself recoloured -- which is the
  // `grayscale(1) drop-shadow(...)` order rather than the other one, and the
  // limit core/Filters.h records.
  //
  // One node for the whole list, GSK taking an array: a view with two drop
  // shadows casts both from one silhouette.
  const guint filter_shadow_count =
      self->has_filters && self->filter_shadows != nullptr ? self->filter_shadows->len : 0;
  if (filter_shadow_count > 0) {
    std::vector<GskShadow> shadows;
    shadows.reserve(filter_shadow_count);
    for (guint i = 0; i < filter_shadow_count; i++) {
      const RnFilterShadow &shadow =
          g_array_index(self->filter_shadows, RnFilterShadow, i);
      shadows.push_back(GskShadow{shadow.color, shadow.dx, shadow.dy, shadow.radius});
    }
    gtk_snapshot_push_shadow(snapshot, shadows.data(), shadows.size());
  }

  if (filter_blurs) {
    gtk_snapshot_push_blur(snapshot, self->filters.blur_radius);
  }
  if (filter_recolours) {
    // GSK applies `transpose(matrix) * pixel + offset` to unpremultiplied RGBA,
    // and graphene reads sixteen floats row by row -- so the matrix goes in
    // transposed, which the rendered-pixel test is what actually pins down.
    graphene_matrix_t matrix;
    float transposed[16];
    for (int row = 0; row < 4; row++) {
      for (int column = 0; column < 4; column++) {
        transposed[row * 4 + column] = self->filters.matrix[column * 4 + row];
      }
    }
    graphene_matrix_init_from_float(&matrix, transposed);
    graphene_vec4_t offset;
    graphene_vec4_init(&offset,
                       self->filters.offset[0],
                       self->filters.offset[1],
                       self->filters.offset[2],
                       self->filters.offset[3]);
    gtk_snapshot_push_color_matrix(snapshot, &matrix, &offset);
  }

  graphene_rect_t bounds;
  bounds.origin.x = 0.0f;
  bounds.origin.y = 0.0f;
  bounds.size.width = static_cast<float>(width);
  bounds.size.height = static_cast<float>(height);

  GskRoundedRect box;
  gsk_rounded_rect_init(&box,
                        &bounds,
                        &self->border_radii[0],
                        &self->border_radii[1],
                        &self->border_radii[2],
                        &self->border_radii[3]);

  // `mixBlendMode` on a child, which CSS blends with its backdrop: everything
  // painted beneath it. GSK's blend node takes a bottom and a top, both appended
  // *after* the push -- so the backdrop has to be inside a push made before any
  // of it is painted, which is why the look-ahead is here rather than in the
  // child loop.
  //
  // One push per blended child, the last one outermost, so the pushes nest the
  // way the blends do: the first blended child sees this view's own content as
  // its backdrop, the second sees that blend's result plus whatever came between
  // them, and so on. Each push is popped in the child loop below, as its child
  // is reached.
  //
  // The backdrop stops at this view, which is a deviation worth naming: CSS
  // blends with everything beneath in the nearest stacking context, which for a
  // plain <View> reaches further up. backlog/correctness.md records it.
  GPtrArray *blended = nullptr;
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (RN_IS_VIEW(child) && RN_VIEW(child)->blends) {
      if (blended == nullptr) {
        blended = g_ptr_array_new();
      }
      g_ptr_array_add(blended, child);
    }
  }
  if (blended != nullptr) {
    // In paint order, which zIndex can differ from. Sorting the blended ones
    // with the same stable comparator the whole list gets below puts them in the
    // order they will be reached, because a stable sort of a subset keeps the
    // subset's order within the sort of the whole.
    g_ptr_array_sort_values(blended, [](gconstpointer a, gconstpointer b) -> int {
      auto *wa = static_cast<GtkWidget *>(const_cast<gpointer>(a));
      auto *wb = static_cast<GtkWidget *>(const_cast<gpointer>(b));
      return rn_view_layout_z_index(RN_VIEW(wa)) - rn_view_layout_z_index(RN_VIEW(wb));
    });
    for (guint i = blended->len; i > 0; i--) {
      gtk_snapshot_push_blend(snapshot, RN_VIEW(g_ptr_array_index(blended, i - 1))->blend_mode);
    }
  }

  // Outset box shadows, behind everything this view draws, which is where CSS
  // puts them: a shadow is cast by the box rather than painted on it.
  //
  // Back to front, because CSS says the first shadow in the list is the one on
  // top and GSK has no z within a snapshot. GSK's node takes the view's own
  // outline and does the geometry -- the spread grows the box, the blur softens
  // it, and the corners stay the view's corners -- so nothing here computes a
  // rectangle. Its dx, dy, spread and blur mean what CSS's do.
  if (self->box_shadows != nullptr) {
    for (guint i = self->box_shadows->len; i > 0; i--) {
      const RnBoxShadow &shadow = g_array_index(self->box_shadows, RnBoxShadow, i - 1);
      if (shadow.inset || shadow.color.alpha <= 0.0f) {
        continue;
      }
      // The blur is clamped, the spread is not. CSS says a blur radius may not
      // be negative and a spread may, and GSK agrees with the first half by
      // asserting on it -- so an app that sends one gets no blur rather than a
      // Gsk-CRITICAL and a missing shadow.
      gtk_snapshot_append_outset_shadow(snapshot,
                                        &box,
                                        &shadow.color,
                                        shadow.dx,
                                        shadow.dy,
                                        shadow.spread,
                                        shadow.blur > 0.0f ? shadow.blur : 0.0f);
    }
  }

  // The background is always clipped to the rounded box, even when children are
  // not: overflow: 'visible' lets a child escape the corner, but the view's own
  // fill still has to respect its border radius.
  if (self->has_background_color) {
    if (self->has_border_radii) {
      gtk_snapshot_push_rounded_clip(snapshot, &box);
    }
    gtk_snapshot_append_color(snapshot, &self->background_color, &bounds);
    if (self->has_border_radii) {
      gtk_snapshot_pop(snapshot);
    }
  }

  // `backgroundImage` gradients, above the background colour and below the
  // content, which is where CSS paints a background image. Clipped to the
  // rounded box for the reason the background colour is: a gradient that
  // squared off the corners would be a worse bug than no gradient.
  //
  // Back to front, the first in the list being the one on top. The geometry and
  // the stops were resolved against the image's own size in the mounting
  // manager, which runs again on every layout, so a resized view is a resized
  // gradient.
  //
  // The image fills `area` and repeats in `tile`, which is what
  // `backgroundSize`, `backgroundPosition` and `backgroundRepeat` come to. The
  // repeat is one `gtk_snapshot_push_repeat`, which tiles in both axes at once
  // from the tile it is given -- so an axis that does not repeat arrives with
  // the painting area as its tile and comes out drawn once. See
  // core/BackgroundLayers.h.
  if (self->gradients != nullptr) {
    for (guint i = self->gradients->len; i > 0; i--) {
      const RnGradientRecord &gradient = g_array_index(self->gradients, RnGradientRecord, i - 1);
      if (gradient.stops == nullptr || gradient.stops->len == 0) {
        continue;
      }
      if (gradient.area.size.width <= 0.0f || gradient.area.size.height <= 0.0f) {
        continue;
      }
      if (self->has_border_radii) {
        gtk_snapshot_push_rounded_clip(snapshot, &box);
      }
      if (gradient.repeats) {
        // The painting area is the border box, which is what CSS clips a
        // background to, and the tile is the period.
        gtk_snapshot_push_repeat(snapshot, &bounds, &gradient.tile);
      }
      // RnGradientStop has GskColorStop's layout, so the array is handed over as
      // it stands rather than copied a field at a time.
      if (gradient.kind == RN_GRADIENT_RADIAL) {
        // 0 and 1 are where the gradient starts and ends along the radius, as a
        // fraction of it: the ending shape is the radius, which is what CSS
        // means by one. GSK fills the rest of the bounds with the last stop,
        // which is also what CSS does past the ending shape -- so the node's
        // bounds are the image's rectangle rather than the box.
        gtk_snapshot_append_radial_gradient(
            snapshot,
            &gradient.area,
            &gradient.center,
            gradient.radius_x,
            gradient.radius_y,
            0.0f,
            1.0f,
            reinterpret_cast<const GskColorStop *>(gradient.stops->data),
            gradient.stops->len);
      } else {
        gtk_snapshot_append_linear_gradient(
            snapshot,
            &gradient.area,
            &gradient.start,
            &gradient.end,
            reinterpret_cast<const GskColorStop *>(gradient.stops->data),
            gradient.stops->len);
      }
      if (gradient.repeats) {
        gtk_snapshot_pop(snapshot);
      }
      if (self->has_border_radii) {
        gtk_snapshot_pop(snapshot);
      }
    }
  }

  // Inset box shadows, above the background and below the content, which is
  // again where CSS puts them: an inset shadow darkens the inside of the box
  // without hiding the text in it.
  if (self->box_shadows != nullptr) {
    for (guint i = self->box_shadows->len; i > 0; i--) {
      const RnBoxShadow &shadow = g_array_index(self->box_shadows, RnBoxShadow, i - 1);
      if (!shadow.inset || shadow.color.alpha <= 0.0f) {
        continue;
      }
      gtk_snapshot_append_inset_shadow(snapshot,
                                       &box,
                                       &shadow.color,
                                       shadow.dx,
                                       shadow.dy,
                                       shadow.spread,
                                       shadow.blur > 0.0f ? shadow.blur : 0.0f);
    }
  }

  // Content clipping is separate, and only happens with overflow: 'hidden'.
  //
  // With a blended child it cannot be one clip around the whole lot: the pops
  // that close each blend happen between the children, and a clip pushed around
  // them would be what those pops closed. So it is pushed around this view's own
  // content here and around each child on its own below, which paints the same
  // picture -- clipping a group and clipping each of its members to the same box
  // are the same thing -- at the cost of one clip node per child.
  const gboolean clips_each_child = self->clips_children && blended != nullptr;
  if (self->clips_children) {
    if (self->has_border_radii) {
      gtk_snapshot_push_rounded_clip(snapshot, &box);
    } else {
      gtk_snapshot_push_clip(snapshot, &bounds);
    }
  }

  if (self->texture != nullptr && width > 0 && height > 0) {
    const float viewWidth = static_cast<float>(width);
    const float viewHeight = static_cast<float>(height);
    const float imageWidth = static_cast<float>(gdk_texture_get_width(self->texture));
    const float imageHeight = static_cast<float>(gdk_texture_get_height(self->texture));

    graphene_rect_t destination;
    destination.origin.x = 0.0f;
    destination.origin.y = 0.0f;
    destination.size.width = viewWidth;
    destination.size.height = viewHeight;

    if (self->texture_fit != RN_IMAGE_FIT_STRETCH && imageWidth > 0 && imageHeight > 0) {
      float scale = 1.0f;
      switch (self->texture_fit) {
        case RN_IMAGE_FIT_CONTAIN:
          scale = MIN(viewWidth / imageWidth, viewHeight / imageHeight);
          break;
        case RN_IMAGE_FIT_COVER:
          scale = MAX(viewWidth / imageWidth, viewHeight / imageHeight);
          break;
        case RN_IMAGE_FIT_CENTER:
          // Centre at natural size, but never larger than the frame -- which is
          // what React Native's `center` does.
          scale = MIN(1.0f, MIN(viewWidth / imageWidth, viewHeight / imageHeight));
          break;
        case RN_IMAGE_FIT_STRETCH:
          break;
      }
      destination.size.width = imageWidth * scale;
      destination.size.height = imageHeight * scale;
      destination.origin.x = (viewWidth - destination.size.width) / 2.0f;
      destination.origin.y = (viewHeight - destination.size.height) / 2.0f;
    }

    // cover and center can put pixels outside the frame, and an <Image> never
    // paints beyond its own box on iOS or Android.
    const gboolean needs_clip = self->texture_fit == RN_IMAGE_FIT_COVER ||
                                self->texture_fit == RN_IMAGE_FIT_CENTER;
    if (needs_clip) {
      graphene_rect_t clip;
      clip.origin.x = 0.0f;
      clip.origin.y = 0.0f;
      clip.size.width = viewWidth;
      clip.size.height = viewHeight;
      gtk_snapshot_push_clip(snapshot, &clip);
    }
    // Tiling wraps everything below it: `gtk_snapshot_push_repeat` takes the
    // area to fill and the rect of one tile, and repeats whatever is recorded
    // until the pop. So the tint's mask node works inside it unchanged.
    const gboolean tiles = self->texture_fit == RN_IMAGE_FIT_REPEAT;
    if (tiles) {
      graphene_rect_t area;
      area.origin.x = 0.0f;
      area.origin.y = 0.0f;
      area.size.width = viewWidth;
      area.size.height = viewHeight;
      gtk_snapshot_push_repeat(snapshot, &area, &destination);
    }
    // Inside the tiling so each tile blurs alike, and around the tint so the
    // blur applies to what is drawn rather than to the silhouette it is masked
    // from.
    const gboolean blurs = self->image_blur > 0.0f;
    if (blurs) {
      gtk_snapshot_push_blur(snapshot, self->image_blur);
    }
    if (self->has_image_tint) {
      // The image becomes a stencil and the colour is what is actually drawn.
      // GskMaskNode records the mask first and the source second, which is why
      // the texture is appended before the colour and there are two pops.
      //
      // Alpha rather than luminance: `tintColor` recolours a silhouette, so
      // what matters is where the image is opaque, not how bright it is.
      gtk_snapshot_push_mask(snapshot, GSK_MASK_MODE_ALPHA);
      gtk_snapshot_append_texture(snapshot, self->texture, &destination);
      gtk_snapshot_pop(snapshot);
      gtk_snapshot_append_color(snapshot, &self->image_tint, &destination);
      gtk_snapshot_pop(snapshot);
    } else {
      gtk_snapshot_append_texture(snapshot, self->texture, &destination);
    }
    if (blurs) {
      gtk_snapshot_pop(snapshot);
    }
    if (tiles) {
      gtk_snapshot_pop(snapshot);
    }
    if (needs_clip) {
      gtk_snapshot_pop(snapshot);
    }
  }

  // Text sits above the background and below any children, which is the order
  // <Text> with nested views expects.
  if (self->text_layout != nullptr) {
    // `numberOfLines` with `ellipsizeMode: 'clip'` is the one truncation Pango
    // leaves to its caller, because a line limit only takes effect while Pango
    // is ellipsizing and clip asks for no ellipsis. The surplus lines are
    // therefore still in this layout, and this is where they are hidden; the
    // measurement reported a box exactly this tall, from the same function, so
    // the cut lands on the edge of the paragraph rather than part way into a
    // line. See rn_pango_clip_height in RnView.h.
    float clip_height = 0.0f;
    const gboolean clips_text = rn_pango_clip_height(self->text_layout, &clip_height);
    if (clips_text) {
      // Vertically only. A line wider than its own box overflows today, for
      // every paragraph on this platform, and a limit on the number of lines is
      // not a reason to start cutting one sideways as well -- so the clip is
      // made wide enough to hold whichever of the two is wider.
      int layout_width = 0;
      int layout_height = 0;
      pango_layout_get_pixel_size(self->text_layout, &layout_width, &layout_height);

      graphene_rect_t text_bounds;
      text_bounds.origin.x = 0.0f;
      text_bounds.origin.y = 0.0f;
      text_bounds.size.width = MAX(bounds.size.width, static_cast<float>(layout_width));
      text_bounds.size.height = clip_height;
      gtk_snapshot_push_clip(snapshot, &text_bounds);
    }
    // The text shadow, which is a shadow of the glyphs' own alpha: GSK's shadow
    // node takes the node it wraps, so one push around the layout is the whole
    // of it. Inside the text's clip, so a shadow cannot escape a paragraph that
    // `numberOfLines` cut.
    const gboolean shadows_text = self->text_shadow_color.alpha > 0.0f;
    if (shadows_text) {
      const GskShadow shadow{self->text_shadow_color,
                             self->text_shadow_dx,
                             self->text_shadow_dy,
                             self->text_shadow_radius};
      gtk_snapshot_push_shadow(snapshot, &shadow, 1);
    }
    gtk_snapshot_append_layout(snapshot, self->text_layout, &self->text_color);
    if (shadows_text) {
      gtk_snapshot_pop(snapshot);
    }
    if (clips_text) {
      gtk_snapshot_pop(snapshot);
    }
  }

  // The clip around this view's own content is closed here when each child is
  // getting its own, so that the pops below close blends rather than a clip.
  if (clips_each_child) {
    gtk_snapshot_pop(snapshot);
  }

  // One child, painted where the blends and the clip expect it. Without a
  // blended child this is `gtk_widget_snapshot_child` and nothing else, which is
  // what it was before any of this.
  guint next_blend = 0;
  auto paint_child = [&](GtkWidget *child) {
    const gboolean is_blended =
        blended != nullptr && next_blend < blended->len &&
        child == static_cast<GtkWidget *>(g_ptr_array_index(blended, next_blend));
    if (is_blended) {
      // Closes the backdrop: everything painted since this child's push, which
      // is this view's content and every child beneath it.
      gtk_snapshot_pop(snapshot);
      next_blend++;
    }
    if (clips_each_child) {
      if (self->has_border_radii) {
        gtk_snapshot_push_rounded_clip(snapshot, &box);
      } else {
        gtk_snapshot_push_clip(snapshot, &bounds);
      }
    }
    gtk_widget_snapshot_child(widget, child, snapshot);
    if (clips_each_child) {
      gtk_snapshot_pop(snapshot);
    }
    if (is_blended) {
      // And the blend node itself, whose result lands in whatever is beneath:
      // the next blend's backdrop, or this view's snapshot.
      gtk_snapshot_pop(snapshot);
    }
  };

  // zIndex only reorders painting. The child list itself stays in mutation
  // order, because Fabric's Insert and Remove index into it.
  gboolean needs_sorting = FALSE;
  for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (RN_IS_VIEW(child) && rn_view_layout_z_index(RN_VIEW(child)) != 0) {
      needs_sorting = TRUE;
      break;
    }
  }

  if (!needs_sorting) {
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
         child = gtk_widget_get_next_sibling(child)) {
      paint_child(child);
    }
  } else {
    GPtrArray *ordered = g_ptr_array_new();
    for (GtkWidget *child = gtk_widget_get_first_child(widget); child != nullptr;
         child = gtk_widget_get_next_sibling(child)) {
      g_ptr_array_add(ordered, child);
    }
    // A stable sort, so equal zIndex keeps document order -- which is what CSS
    // and React Native both promise.
    g_ptr_array_sort_values(ordered, [](gconstpointer a, gconstpointer b) -> int {
      auto *wa = static_cast<GtkWidget *>(const_cast<gpointer>(a));
      auto *wb = static_cast<GtkWidget *>(const_cast<gpointer>(b));
      const int za = RN_IS_VIEW(wa) ? rn_view_layout_z_index(RN_VIEW(wa)) : 0;
      const int zb = RN_IS_VIEW(wb) ? rn_view_layout_z_index(RN_VIEW(wb)) : 0;
      return za - zb;
    });
    for (guint i = 0; i < ordered->len; i++) {
      paint_child(GTK_WIDGET(g_ptr_array_index(ordered, i)));
    }
    g_ptr_array_free(ordered, TRUE);
  }

  if (blended != nullptr) {
    // Any push the loop did not reach, closed so the snapshot stays balanced.
    // Nothing should get here -- every blended child is in the list the loop
    // walks -- but an unbalanced snapshot is a crash in GSK rather than a wrong
    // picture, so it is worth two lines.
    for (guint i = next_blend; i < blended->len; i++) {
      gtk_snapshot_pop(snapshot);
      gtk_snapshot_pop(snapshot);
    }
    g_ptr_array_free(blended, TRUE);
    blended = nullptr;
  }

  if (self->clips_children && !clips_each_child) {
    gtk_snapshot_pop(snapshot);
  }

  // The scrollbars: an overlay, so above the content and above any children,
  // which is what "overlay indicator" means -- and below the border, the
  // DevTools overlay and the focus ring, none of which belongs to the app.
  if (self->indicator_v_length > 0.0 || self->indicator_h_length > 0.0) {
    const float thickness = static_cast<float>(basalt::kScrollIndicatorThickness);
    const float inset = static_cast<float>(basalt::kScrollIndicatorInset);
    // Neutral and translucent, so it reads over light and dark content alike,
    // and white instead when `indicatorStyle` asked for it -- core decides
    // which, so all three hosts answer the prop with the same colour.
    const GdkRGBA thumb = self->indicator_colour;

    if (self->indicator_v_length > 0.0) {
      const graphene_rect_t bar =
          GRAPHENE_RECT_INIT(static_cast<float>(width) - thickness - inset,
                             static_cast<float>(self->indicator_v_offset),
                             thickness,
                             static_cast<float>(self->indicator_v_length));
      GskRoundedRect rounded;
      gsk_rounded_rect_init_from_rect(&rounded, &bar, thickness / 2.0F);
      gtk_snapshot_push_rounded_clip(snapshot, &rounded);
      gtk_snapshot_append_color(snapshot, &thumb, &bar);
      gtk_snapshot_pop(snapshot);
    }
    if (self->indicator_h_length > 0.0) {
      const graphene_rect_t bar =
          GRAPHENE_RECT_INIT(static_cast<float>(self->indicator_h_offset),
                             static_cast<float>(height) - thickness - inset,
                             static_cast<float>(self->indicator_h_length),
                             thickness);
      GskRoundedRect rounded;
      gsk_rounded_rect_init_from_rect(&rounded, &bar, thickness / 2.0F);
      gtk_snapshot_push_rounded_clip(snapshot, &rounded);
      gtk_snapshot_append_color(snapshot, &thumb, &bar);
      gtk_snapshot_pop(snapshot);
    }
  }

  // Borders paint over the content, as they do on every other platform.
  if (self->has_borders) {
    if (self->border_style == RN_BORDER_SOLID) {
      gtk_snapshot_append_border(snapshot, &box, self->border_widths, self->border_colors);
    } else {
      // Dotted and dashed are stroked, GTK's border node painting solid only.
      // One path around the rounded rectangle and one stroke, which is why the
      // style is a property of the whole outline rather than of a side: a
      // stroked path carries one dash pattern.
      //
      // Inset by half the width, because a stroke straddles its path while a
      // border node sits inside the box. Without that a 4pt dashed border would
      // paint two points outside the view and overlap its neighbour.
      const float width = self->border_widths[0] > 0.0f ? self->border_widths[0] : 1.0f;
      GskRoundedRect centred = box;
      graphene_rect_inset(&centred.bounds, width / 2.0f, width / 2.0f);

      GskPathBuilder *builder = gsk_path_builder_new();
      gsk_path_builder_add_rounded_rect(builder, &centred);
      GskPath *path = gsk_path_builder_free_to_path(builder);

      GskStroke *stroke = gsk_stroke_new(width);
      // Scaled to the width, the way a browser does it: a fixed pattern reads as
      // a hairline on a thick border and as a solid line on a thin one. Dotted
      // is round caps on a zero-length dash, which is what makes a dot a dot
      // rather than a short dash.
      if (self->border_style == RN_BORDER_DOTTED) {
        const float dots[2] = {0.0f, width * 2.0f};
        gsk_stroke_set_line_cap(stroke, GSK_LINE_CAP_ROUND);
        gsk_stroke_set_dash(stroke, dots, 2);
      } else {
        const float dashes[2] = {width * 3.0f, width * 2.0f};
        gsk_stroke_set_dash(stroke, dashes, 2);
      }

      gtk_snapshot_append_stroke(snapshot, path, stroke, &self->border_colors[0]);

      gsk_stroke_free(stroke);
      gsk_path_unref(path);
    }
  }

  // React DevTools' overlay, over everything including the border. Above the
  // app on purpose: it is not part of it, and an inspected element half hidden
  // behind a card would be pointing at the wrong thing.
  if (self->highlights != nullptr) {
    const auto *values = &g_array_index(self->highlights, float, 0);
    const auto *filled = &g_array_index(self->highlight_filled, gboolean, 0);
    const guint count = self->highlight_filled->len;
    for (guint i = 0; i < count; i++) {
      const float *rectangle = values + (i * 8);
      graphene_rect_t area;
      area.origin.x = rectangle[0];
      area.origin.y = rectangle[1];
      area.size.width = rectangle[2];
      area.size.height = rectangle[3];
      const GdkRGBA colour{rectangle[4], rectangle[5], rectangle[6], rectangle[7]};

      if (filled[i]) {
        gtk_snapshot_append_color(snapshot, &colour, &area);
      }
      GskRoundedRect outline;
      gsk_rounded_rect_init_from_rect(&outline, &area, 0.0f);
      const float widths[4] = {basalt::kHighlightBorderWidth,
                               basalt::kHighlightBorderWidth,
                               basalt::kHighlightBorderWidth,
                               basalt::kHighlightBorderWidth};
      // Opaque on the outline even when the fill is not, so the edge of an
      // inspected element is a line rather than a suggestion.
      const GdkRGBA edge{rectangle[4], rectangle[5], rectangle[6], 1.0f};
      const GdkRGBA colors[4] = {edge, edge, edge, edge};
      gtk_snapshot_append_border(snapshot, &outline, widths, colors);
    }
  }

  // CSS's outline, outside the box and over everything: it is not a border, it
  // takes no layout space, and it sits `offset` away from the border edge.
  //
  // Nothing clips it, which is the point -- a ring inside the box would be a
  // border. The rounded rect it follows is the view's own grown by the offset
  // plus the width, with each non-zero radius grown to match so the ring stays
  // concentric with a rounded card. A corner that was square stays square,
  // which is what React Native's iOS half does too.
  if (self->outline_width > 0.0f && self->outline_color.alpha > 0.0f) {
    const float grow = self->outline_width + self->outline_offset;
    GskRoundedRect ring = box;
    graphene_rect_inset(&ring.bounds, -grow, -grow);
    for (int corner = 0; corner < 4; corner++) {
      if (ring.corner[corner].width > 0.0f) {
        ring.corner[corner].width += grow;
      }
      if (ring.corner[corner].height > 0.0f) {
        ring.corner[corner].height += grow;
      }
    }

    if (self->outline_style == RN_BORDER_SOLID) {
      const float widths[4] = {self->outline_width,
                               self->outline_width,
                               self->outline_width,
                               self->outline_width};
      const GdkRGBA colors[4] = {self->outline_color,
                                 self->outline_color,
                                 self->outline_color,
                                 self->outline_color};
      gtk_snapshot_append_border(snapshot, &ring, widths, colors);
    } else {
      // Dotted and dashed, stroked for the reason a dashed border is: GTK's
      // border node paints solid only. Inset by half the width, a stroke
      // straddling its path where a border node sits inside the box.
      GskRoundedRect centred = ring;
      graphene_rect_inset(&centred.bounds, self->outline_width / 2.0f, self->outline_width / 2.0f);

      GskPathBuilder *builder = gsk_path_builder_new();
      gsk_path_builder_add_rounded_rect(builder, &centred);
      GskPath *path = gsk_path_builder_free_to_path(builder);

      GskStroke *stroke = gsk_stroke_new(self->outline_width);
      if (self->outline_style == RN_BORDER_DOTTED) {
        const float dots[2] = {0.0f, self->outline_width * 2.0f};
        gsk_stroke_set_line_cap(stroke, GSK_LINE_CAP_ROUND);
        gsk_stroke_set_dash(stroke, dots, 2);
      } else {
        const float dashes[2] = {self->outline_width * 3.0f, self->outline_width * 2.0f};
        gsk_stroke_set_dash(stroke, dashes, 2);
      }
      gtk_snapshot_append_stroke(snapshot, path, stroke, &self->outline_color);
      gsk_stroke_free(stroke);
      gsk_path_unref(path);
    }
  }

  // The focus ring, over everything including the border, because it is the
  // answer to "where am I" and must not be hidden by what it is drawn on.
  //
  // Drawn rather than delegated: GTK's own focus rendering is a CSS outline on
  // a themed widget, and these widgets have no theme -- React Native decided
  // every colour here. The ring follows the view's own corner radii so it hugs
  // a rounded button, and `has-visible-focus` is what keeps it from appearing
  // when focus was moved by a click rather than by the keyboard.
  if (gtk_widget_has_visible_focus(widget)) {
    // Inside the view's own bounds rather than around them. The AppKit side
    // draws its ring in a drawRect: that is clipped to the view, so a ring that
    // sat outside here and inside there would be a difference an app did not
    // ask for -- and one that would only show up as a pixel diff.
    GskRoundedRect ring = box;
    const float widths[4] = {basalt::kFocusRingWidth,
                             basalt::kFocusRingWidth,
                             basalt::kFocusRingWidth,
                             basalt::kFocusRingWidth};
    const GdkRGBA ringColor{basalt::kFocusRingRed,
                            basalt::kFocusRingGreen,
                            basalt::kFocusRingBlue,
                            basalt::kFocusRingAlpha};
    const GdkRGBA colors[4] = {ringColor, ringColor, ringColor, ringColor};
    gtk_snapshot_append_border(snapshot, &ring, widths, colors);
  }

  if (filter_recolours) {
    gtk_snapshot_pop(snapshot);
  }
  if (filter_blurs) {
    gtk_snapshot_pop(snapshot);
  }
  if (filter_shadow_count > 0) {
    gtk_snapshot_pop(snapshot);
  }
  if (needs_opacity_layer) {
    gtk_snapshot_pop(snapshot);
  }
}

static void rn_view_dispose(GObject *object) {
  RnView *self = RN_VIEW(object);
  GtkWidget *widget = GTK_WIDGET(object);

  // A GtkWidget must unparent its children before it goes away, or GTK warns
  // and leaks. Delete mutations can arrive with children still attached.
  GtkWidget *child = gtk_widget_get_first_child(widget);
  while (child != nullptr) {
    GtkWidget *next = gtk_widget_get_next_sibling(child);
    gtk_widget_unparent(child);
    child = next;
  }

  // The generic loop above already unparented them, so this only drops the
  // borrowed pointers.
  self->editable = nullptr;
  self->control = nullptr;
  self->control_kind = RN_CONTROL_NONE;
  g_clear_pointer(&self->control_description, g_free);
  g_clear_pointer(&self->highlights, g_array_unref);
  g_clear_pointer(&self->highlight_filled, g_array_unref);

  g_clear_object(&self->text_layout);
  g_clear_object(&self->texture);
  // The tick callback holds no reference, so it has to come off before the
  // widget goes: GTK warns about a callback on a finalised widget.
  if (self->animation_tick != 0) {
    gtk_widget_remove_tick_callback(GTK_WIDGET(self), self->animation_tick);
    self->animation_tick = 0;
  }
  g_clear_object(&self->animation_iter);
  g_clear_object(&self->animation);
  g_clear_pointer(&self->role_name, g_free);
  g_clear_pointer(&self->test_id, g_free);
  g_clear_pointer(&self->accessibility_order, g_array_unref);
  g_clear_pointer(&self->native_id, g_free);
  g_clear_pointer(&self->cursor_name, g_free);
  g_clear_pointer(&self->blend_name, g_free);
  g_clear_pointer(&self->filter_shadows, g_array_unref);
  g_clear_pointer(&self->box_shadows, g_array_unref);
  g_clear_pointer(&self->gradients, g_array_unref);
  g_clear_pointer(&self->labelled_by, g_array_unref);
  g_clear_pointer(&self->last_announcement, g_free);

  G_OBJECT_CLASS(rn_view_parent_class)->dispose(object);
}

// `hitSlop`: the widget answers for points outside its own allocation.
//
// `contains` is what `gtk_widget_pick` asks of each widget, so widening it is
// the whole implementation -- and it is the reason the slop applies to a hover
// and a drop as well as to a press, all three going through one pick. The
// parent's answer is unchanged, so a slop that reaches outside the parent is
// only reachable where the parent is: GTK picks children of a widget it is
// already inside, which is the same bound iOS has.
static gboolean rn_view_contains(GtkWidget *widget, double x, double y) {
  RnView *self = RN_VIEW(widget);
  if (self->has_hit_slop) {
    const double width = gtk_widget_get_width(widget);
    const double height = gtk_widget_get_height(widget);
    if (x >= -self->hit_slop[3] && x < width + self->hit_slop[1] &&
        y >= -self->hit_slop[0] && y < height + self->hit_slop[2]) {
      return TRUE;
    }
  }
  return GTK_WIDGET_CLASS(rn_view_parent_class)->contains(widget, x, y);
}

static void rn_view_class_init(RnViewClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = rn_view_dispose;
  widget_class->snapshot = rn_view_snapshot;
  widget_class->contains = rn_view_contains;

  gtk_widget_class_set_layout_manager_type(widget_class, RN_TYPE_LAYOUT);
}

static void rn_view_init(RnView *self) {
  self->tag = 0;
  graphene_rect_init(&self->frame, 0.0f, 0.0f, 0.0f, 0.0f);
  self->has_background_color = FALSE;
  self->background_color = GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  self->opacity = 1.0;
  self->text_layout = nullptr;
  self->text_color = GdkRGBA{0.0f, 0.0f, 0.0f, 1.0f};
  self->texture = nullptr;
  self->texture_fit = RN_IMAGE_FIT_COVER;
  self->role_name = nullptr;
  self->test_id = nullptr;
  self->accessible_modal = FALSE;
  self->writing_direction = nullptr;
  self->spell_check = nullptr;
  self->auto_correct = nullptr;
  self->auto_capitalize = nullptr;
  self->keyboard_type = nullptr;
  self->cursor_name = nullptr;
  self->box_shadows = nullptr;
  self->gradients = nullptr;
  self->labelled_by = nullptr;
  self->accessibility_order = nullptr;
  self->last_announcement = nullptr;
  self->caret_hidden = FALSE;
  self->context_menu_hidden = FALSE;
  self->clips_children = FALSE;
  self->scroll_x = 0.0;
  self->scroll_y = 0.0;
  // The thumb's colour, which `indicatorStyle` replaces. Core's own default,
  // asked for rather than spelled, so the three hosts cannot drift.
  const basalt::ScrollIndicatorColour thumb =
      basalt::scrollIndicatorColourFor(basalt::ScrollIndicatorStyle::Default);
  self->indicator_colour = GdkRGBA{thumb.red, thumb.green, thumb.blue, thumb.alpha};
  for (int i = 0; i < 4; i++) {
    self->border_radii[i] = graphene_size_t{0.0f, 0.0f};
    self->border_widths[i] = 0.0f;
    self->border_colors[i] = GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  }
  self->has_border_radii = FALSE;
  self->has_borders = FALSE;
  self->text_shadow_dx = 0.0f;
  self->text_shadow_dy = 0.0f;
  self->text_shadow_radius = 0.0f;
  self->text_shadow_color = GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  self->filter_shadows = nullptr;
  self->blend_name = nullptr;
  self->blend_mode = GSK_BLEND_MODE_DEFAULT;
  self->blends = FALSE;
  self->outline_width = 0.0f;
  self->outline_offset = 0.0f;
  self->outline_color = GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  self->outline_style = RN_BORDER_SOLID;
  self->has_filters = FALSE;
  self->has_hit_slop = FALSE;
  for (int edge = 0; edge < 4; edge++) {
    self->hit_slop[edge] = 0.0f;
  }
  graphene_matrix_init_identity(&self->transform);
  self->has_transform = FALSE;
  self->z_index = 0;
  self->editable = nullptr;
  self->control = nullptr;
  self->control_kind = RN_CONTROL_NONE;
  self->control_disabled = FALSE;
  self->control_description = nullptr;
  self->highlights = nullptr;
  self->highlight_filled = nullptr;
  self->resize_callback = nullptr;
  self->resize_data = nullptr;
  // -1, not 0: a first allocation of 0x0 is a real transition worth reporting.
  self->allocated_width = -1;
  self->allocated_height = -1;
}

RnView *rn_view_new(int tag) {
  return rn_view_new_with_role(tag, GTK_ACCESSIBLE_ROLE_GENERIC);
}

RnView *rn_view_new_with_role(int tag, GtkAccessibleRole role) {
  // accessible-role is construct-only, so it goes here rather than in a setter.
  RnView *self = RN_VIEW(g_object_new(RN_TYPE_VIEW, "accessible-role", role, nullptr));
  self->tag = tag;
  return self;
}

GtkWidget *rn_view_set_editable(RnView *self, gboolean editable, gboolean multiline) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);

  if (!editable) {
    if (self->editable != nullptr) {
      gtk_widget_unparent(self->editable);
      self->editable = nullptr;
    }
    return nullptr;
  }

  // `multiline` is not a property either peer has: a field that changes between
  // the two is a different widget, so the old one goes. React Native does this
  // to `secureTextEntry` on AppKit for the same reason.
  char *carried = nullptr;
  if (self->editable != nullptr &&
      rn_peer_is_multiline(self->editable) != (multiline ? TRUE : FALSE)) {
    // The contents come across. Nothing asked for the field to be cleared --
    // React changed one prop -- and the controlled loop will not put the text
    // back by itself, because from its side the `text` prop did not change.
    // AppKit carries the text the same way when secureTextEntry rebuilds its
    // field, and for the same reason.
    carried = rn_peer_get_text(self->editable);
    gtk_widget_unparent(self->editable);
    self->editable = nullptr;
  }

  if (self->editable == nullptr) {
    self->editable = rn_peer_new(multiline);
    if (carried != nullptr) {
      rn_peer_set_text(self->editable, carried);
    }
    // No frame of its own: the RnView draws the background and border from
    // React Native's props, and a second one underneath would double them.
    gtk_widget_add_css_class(self->editable, "rn-text-input");
    gtk_widget_set_parent(self->editable, GTK_WIDGET(self));
  }
  g_free(carried);
  return self->editable;
}

GtkWidget *rn_view_get_editable(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);
  return self->editable;
}

GtkWidget *rn_view_set_control(RnView *self, RnControlKind kind) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);

  // A view never changes which control it is -- React remounts rather than
  // turning a switch into a spinner -- but a Delete that arrives as an Update
  // would, and a stale GtkSwitch inside a spinner's frame is the kind of thing
  // that is only visible in a screenshot.
  if (self->control != nullptr && self->control_kind != kind) {
    gtk_widget_unparent(self->control);
    self->control = nullptr;
    self->control_kind = RN_CONTROL_NONE;
  }

  if (kind == RN_CONTROL_NONE) {
    if (self->control != nullptr) {
      gtk_widget_unparent(self->control);
      self->control = nullptr;
    }
    self->control_kind = RN_CONTROL_NONE;
    return nullptr;
  }

  if (self->control == nullptr) {
    self->control = kind == RN_CONTROL_SWITCH ? gtk_switch_new() : gtk_spinner_new();
    self->control_kind = kind;
    // Centred at its natural size by rn_layout_allocate, which is where the
    // reason is written down.
    gtk_widget_add_css_class(self->control, "rn-control");
    gtk_widget_set_parent(self->control, GTK_WIDGET(self));
  }
  return self->control;
}

GtkWidget *rn_view_get_control(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);
  return self->control;
}

RnControlKind rn_view_get_control_kind(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), RN_CONTROL_NONE);
  return self->control_kind;
}

void rn_view_set_highlights(RnView *self,
                            const float *rectangles,
                            const gboolean *filled,
                            int count) {
  g_return_if_fail(RN_IS_VIEW(self));

  g_clear_pointer(&self->highlights, g_array_unref);
  g_clear_pointer(&self->highlight_filled, g_array_unref);
  if (count > 0 && rectangles != nullptr) {
    self->highlights = g_array_sized_new(FALSE, FALSE, sizeof(float), count * 8);
    g_array_append_vals(self->highlights, rectangles, count * 8);
    self->highlight_filled = g_array_sized_new(FALSE, FALSE, sizeof(gboolean), count);
    g_array_append_vals(self->highlight_filled, filled, count);
  }
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_control_disabled(RnView *self, gboolean disabled) {
  g_return_if_fail(RN_IS_VIEW(self));
  self->control_disabled = disabled;
}

gboolean rn_view_get_control_disabled(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), FALSE);
  return self->control_disabled;
}

void rn_view_set_control_description(RnView *self, const char *description) {
  g_return_if_fail(RN_IS_VIEW(self));
  g_clear_pointer(&self->control_description, g_free);
  if (description != nullptr && *description != '\0') {
    self->control_description = g_strdup(description);
  }
}

void rn_view_set_accessible_text(RnView *self, const char *label, const char *description) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (label != nullptr && *label != '\0') {
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_LABEL, label, -1);
  }
  if (description != nullptr && *description != '\0') {
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_DESCRIPTION, description, -1);
  }
}

void rn_view_set_input_hiding(RnView *self,
                              gboolean caret_hidden,
                              gboolean context_menu_hidden) {
  if (!RN_IS_VIEW(self)) {
    return;
  }
  self->caret_hidden = caret_hidden;
  self->context_menu_hidden = context_menu_hidden;
}

void rn_view_set_input_kinds(RnView *self, const char *auto_capitalize, const char *keyboard_type) {
  g_return_if_fail(RN_IS_VIEW(self));
  self->auto_capitalize = auto_capitalize;
  self->keyboard_type = keyboard_type;
}

void rn_view_set_text_checking(RnView *self, const char *spell_check, const char *auto_correct) {
  g_return_if_fail(RN_IS_VIEW(self));
  // Static strings from core/TextChecking.h, not copies.
  self->spell_check = spell_check;
  self->auto_correct = auto_correct;
}

void rn_view_set_writing_direction(RnView *self, const char *direction) {
  g_return_if_fail(RN_IS_VIEW(self));
  // A literal from the mounting manager rather than a copy: the three names are
  // static strings, and nothing else ever sets this.
  self->writing_direction = direction;
}

void rn_view_set_accessible_modal(RnView *self, gboolean modal) {
  g_return_if_fail(RN_IS_VIEW(self));
  self->accessible_modal = modal;
  // ARIA's `aria-modal`, which is what GTK's property is: it tells a screen
  // reader to stay inside this element rather than wandering through the views
  // behind it, which is React Native's `accessibilityViewIsModal`.
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_MODAL, modal, -1);
}

static void rn_view_apply_flag(RnView *self, GtkAccessibleState state, RnAccessibleFlag flag) {
  switch (flag) {
    case RN_A11Y_UNSET:
      // Leaving a state alone is not the same as setting it false: a view that
      // never says anything about "checked" is not an unchecked checkbox.
      gtk_accessible_reset_state(GTK_ACCESSIBLE(self), state);
      return;
    case RN_A11Y_FALSE:
    case RN_A11Y_TRUE:
      break;
  }

  const gboolean value = flag == RN_A11Y_TRUE ? TRUE : FALSE;
  if (state == GTK_ACCESSIBLE_STATE_CHECKED) {
    gtk_accessible_update_state(GTK_ACCESSIBLE(self),
                                state,
                                value ? GTK_ACCESSIBLE_TRISTATE_TRUE : GTK_ACCESSIBLE_TRISTATE_FALSE,
                                -1);
    return;
  }
  gtk_accessible_update_state(GTK_ACCESSIBLE(self), state, value, -1);
}

void rn_view_set_image_blur(RnView *self, float radius) {
  g_return_if_fail(RN_IS_VIEW(self));

  const float wanted = radius > 0.0f ? radius : 0.0f;
  if (self->image_blur == wanted) {
    return;
  }
  self->image_blur = wanted;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

// The text of one view and everything under it, appended to `out`.
//
// Paragraphs only. An accessible label can stand in for text, but GTK offers no
// way to read one back -- `gtk_accessible_update_property` is write-only outside
// the test helpers -- so the mounting manager prefers the label from the props
// before asking for this, which is also where the AppKit host decides it.
static void rn_view_collect_text_into(RnView *self, GString *out) {
  if (self->text_layout != nullptr) {
    const char *text = pango_layout_get_text(self->text_layout);
    if (text != nullptr && *text != '\0') {
      if (out->len > 0) {
        g_string_append_c(out, ' ');
      }
      g_string_append(out, text);
    }
  }
  for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(self)); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (RN_IS_VIEW(child)) {
      rn_view_collect_text_into(RN_VIEW(child), out);
    }
  }
}

char *rn_view_collect_text(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), g_strdup(""));

  GString *out = g_string_new(nullptr);
  rn_view_collect_text_into(self, out);
  return g_string_free(out, FALSE);
}

void rn_view_announce(RnView *self, const char *text, gboolean assertive) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (text == nullptr || *text == '\0') {
    return;
  }

  g_free(self->last_announcement);
  self->last_announcement = g_strdup(text);

#if GTK_CHECK_VERSION(4, 14, 0)
  gtk_accessible_announce(GTK_ACCESSIBLE(self),
                          text,
                          assertive ? GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_HIGH
                                    : GTK_ACCESSIBLE_ANNOUNCEMENT_PRIORITY_MEDIUM);
#else
  // GTK 4.10 to 4.13, which this project still supports: announcements arrived
  // in 4.14. The text is still recorded, so a run on an older GTK reports what
  // it would have said rather than looking as though the prop did nothing.
  (void)assertive;
#endif
}

const char *rn_view_get_last_announcement(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);
  return self->last_announcement;
}

// One accessible relation that takes a list of views, applied or reset.
//
// Shared by `labelled-by` and the reading order, which differ only in the
// relation and in which tags the dump remembers. The awkward parts below are
// GTK's and are the same for both.
static void rn_view_apply_relation_list(RnView *self,
                                        GtkAccessibleRelation relation,
                                        RnView **views,
                                        guint count) {
  if (count == 0) {
    gtk_accessible_reset_relation(GTK_ACCESSIBLE(self), relation);
    return;
  }

#if GTK_CHECK_VERSION(4, 14, 0)
  // A GtkAccessibleList, which is the only way to pass a list whose length is
  // not known where the call is written: `gtk_accessible_update_relation` takes
  // its references as varargs.
  //
  // Built from a GList rather than from an array, because
  // `gtk_accessible_list_new_from_array` is unusable: its guard reads
  // `accessibles == NULL || n_accessibles == 0`, so every non-empty array is
  // refused with a Gtk-CRITICAL and a NULL return. Measured on GTK 4.22.4, and
  // recorded in backlog/upstream.md -- `new_from_list` has the right guard and
  // the same effect.
  GList *references = nullptr;
  for (guint i = count; i > 0; i--) {
    references = g_list_prepend(references, views[i - 1]);
  }
  GtkAccessibleList *list = gtk_accessible_list_new_from_list(references);
  g_list_free(references);
  GValue value = G_VALUE_INIT;
  g_value_init(&value, GTK_ACCESSIBLE_LIST);
  // Taken rather than set: a boxed value copies what it is given, and
  // GtkAccessibleList has no unref of its own to pair with that copy. The
  // g_value_unset below is what frees it.
  g_value_take_boxed(&value, list);
  gtk_accessible_update_relation_value(GTK_ACCESSIBLE(self), 1, &relation, &value);
  g_value_unset(&value);
#else
  // GTK 4.10 to 4.13, which this project still supports: GtkAccessibleList
  // arrived in 4.14, and the varargs form can only carry a list written out in
  // full. The first reference is taken and the rest are dropped, one label being
  // what `labelled-by` almost always carries.
  gtk_accessible_update_relation(GTK_ACCESSIBLE(self), relation, views[0], nullptr, -1);
#endif
}

// The tags a relation resolved to, remembered for the tree dump.
static void rn_view_remember_tags(GArray **into, RnView **views, guint count) {
  if (count == 0) {
    g_clear_pointer(into, g_array_unref);
    return;
  }
  if (*into == nullptr) {
    *into = g_array_sized_new(FALSE, FALSE, sizeof(int), count);
  } else {
    g_array_set_size(*into, 0);
  }
  for (guint i = 0; i < count; i++) {
    const int tag = rn_view_get_tag(views[i]);
    g_array_append_val(*into, tag);
  }
}

void rn_view_set_labelled_by(RnView *self, RnView **labels, int count) {
  g_return_if_fail(RN_IS_VIEW(self));

  const guint wanted = labels != nullptr && count > 0 ? static_cast<guint>(count) : 0;
  rn_view_remember_tags(&self->labelled_by, labels, wanted);
  rn_view_apply_relation_list(
      self, GTK_ACCESSIBLE_RELATION_LABELLED_BY, labels, wanted);
}

// `experimental_accessibilityOrder`: the children this view wants read, in that
// order.
//
// ARIA's `aria-flowto`, which GTK spells `GTK_ACCESSIBLE_RELATION_FLOW_TO`:
// "from here, read these next". Set on the view that asked rather than as a
// chain between the children -- `c1` flows to `c2`, `c2` to `c3` -- which is
// the other reading of the same relation. One call on one view is resettable in
// one call and needs no record of which children were in the last order;
// backlog/accessibility.md records the choice and the alternative.
void rn_view_set_accessibility_order(RnView *self, RnView **children, int count) {
  g_return_if_fail(RN_IS_VIEW(self));

  const guint wanted = children != nullptr && count > 0 ? static_cast<guint>(count) : 0;
  rn_view_remember_tags(&self->accessibility_order, children, wanted);
  rn_view_apply_relation_list(self, GTK_ACCESSIBLE_RELATION_FLOW_TO, children, wanted);
}

void rn_view_set_accessible_role_description(RnView *self, const char *description) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (description == nullptr || *description == '\0') {
    gtk_accessible_reset_property(GTK_ACCESSIBLE(self),
                                  GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION);
    return;
  }
  gtk_accessible_update_property(
      GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_ROLE_DESCRIPTION, description, -1);
}

void rn_view_set_accessible_value(RnView *self, int min, int max, int now, const char *text) {
  g_return_if_fail(RN_IS_VIEW(self));

  // Each property separately, because React Native's four are independent and a
  // caller may give a `now` with no range or a text form with no number at all.
  // `gtk_accessible_reset_property` is what says "this view has nothing to say
  // about that" as distinct from saying zero.
  if (min == RN_A11Y_VALUE_UNSET) {
    gtk_accessible_reset_property(GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_VALUE_MIN);
  } else {
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_VALUE_MIN, static_cast<double>(min), -1);
  }

  if (max == RN_A11Y_VALUE_UNSET) {
    gtk_accessible_reset_property(GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_VALUE_MAX);
  } else {
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_VALUE_MAX, static_cast<double>(max), -1);
  }

  if (now == RN_A11Y_VALUE_UNSET) {
    gtk_accessible_reset_property(GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_VALUE_NOW);
  } else {
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_VALUE_NOW, static_cast<double>(now), -1);
  }

  if (text == nullptr || *text == '\0') {
    gtk_accessible_reset_property(GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT);
  } else {
    gtk_accessible_update_property(
        GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PROPERTY_VALUE_TEXT, text, -1);
  }
}

void rn_view_set_accessible_state(RnView *self,
                                  RnAccessibleFlag disabled,
                                  RnAccessibleFlag checked,
                                  RnAccessibleFlag selected,
                                  RnAccessibleFlag expanded,
                                  RnAccessibleFlag busy) {
  g_return_if_fail(RN_IS_VIEW(self));

  rn_view_apply_flag(self, GTK_ACCESSIBLE_STATE_DISABLED, disabled);
  rn_view_apply_flag(self, GTK_ACCESSIBLE_STATE_CHECKED, checked);
  rn_view_apply_flag(self, GTK_ACCESSIBLE_STATE_SELECTED, selected);
  rn_view_apply_flag(self, GTK_ACCESSIBLE_STATE_EXPANDED, expanded);
  rn_view_apply_flag(self, GTK_ACCESSIBLE_STATE_BUSY, busy);
}

void rn_view_set_accessible_hidden(RnView *self, gboolean hidden) {
  g_return_if_fail(RN_IS_VIEW(self));
  gtk_accessible_update_state(GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_STATE_HIDDEN, hidden, -1);
}

int rn_view_get_tag(RnView *self) {
  return self->tag;
}

void rn_view_set_peer_insets(RnView *self, const GtkBorder *insets) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (memcmp(&self->peer_insets, insets, sizeof(GtkBorder)) == 0) {
    return;
  }
  self->peer_insets = *insets;
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

void rn_view_get_peer_insets(RnView *self, GtkBorder *out) {
  g_return_if_fail(RN_IS_VIEW(self));
  *out = self->peer_insets;
}

void rn_view_set_frame(RnView *self, float x, float y, float width, float height) {
  graphene_rect_init(&self->frame, x, y, width, height);

  // The frame lives in the parent's coordinate space, so it is the parent's
  // allocation that has to be redone.
  GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(self));
  gtk_widget_queue_allocate(parent != nullptr ? parent : GTK_WIDGET(self));
}

void rn_view_get_frame(RnView *self, graphene_rect_t *out) {
  *out = self->frame;
}

void rn_view_set_background_color(RnView *self, gboolean has_color, const GdkRGBA *color) {
  self->has_background_color = has_color;
  if (has_color && color != nullptr) {
    self->background_color = *color;
  }
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_opacity(RnView *self, double opacity) {
  self->opacity = CLAMP(opacity, 0.0, 1.0);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_text_shadow(
    RnView *self, float dx, float dy, float radius, const GdkRGBA *color) {
  g_return_if_fail(RN_IS_VIEW(self));

  const GdkRGBA wanted =
      color != nullptr ? *color : GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  if (self->text_shadow_dx == dx && self->text_shadow_dy == dy &&
      self->text_shadow_radius == radius &&
      gdk_rgba_equal(&self->text_shadow_color, &wanted)) {
    return;
  }
  self->text_shadow_dx = dx;
  self->text_shadow_dy = dy;
  self->text_shadow_radius = radius > 0.0f ? radius : 0.0f;
  self->text_shadow_color = wanted;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_text_layout(RnView *self, PangoLayout *layout, const GdkRGBA *color) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (layout != nullptr) {
    g_object_ref(layout);
  }
  g_clear_object(&self->text_layout);
  self->text_layout = layout;

  if (color != nullptr) {
    self->text_color = *color;
  }

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

gboolean rn_pango_clip_height(PangoLayout *layout, float *out_height) {
  g_return_val_if_fail(out_height != nullptr, FALSE);

  if (layout == nullptr) {
    return FALSE;
  }

  // An ellipsizing layout has already been cut by Pango itself, which is why
  // head, middle and tail have never needed any of this. Clipping one a second
  // time could only shave the bottom off a line Pango meant to keep.
  if (pango_layout_get_ellipsize(layout) != PANGO_ELLIPSIZE_NONE) {
    return FALSE;
  }

  // A negative height is how the layout carries `numberOfLines`, and zero means
  // no limit was asked for.
  //
  // Zero is not Pango's default, which is the trap here: a fresh layout answers
  // -1, measured rather than assumed, and -1 is also exactly what a
  // `numberOfLines={1}` layout carries. The two are indistinguishable from the
  // layout alone, so buildTextLayout sets the height to zero explicitly when
  // there is no limit. Without that this reads every ordinary paragraph as a
  // one-line one and cuts the whole screen down to its first line.
  const int height = pango_layout_get_height(layout);
  if (height >= 0) {
    return FALSE;
  }

  const int limit = -height;
  if (pango_layout_get_line_count(layout) <= limit) {
    // Fewer lines than it is allowed, so nothing is being hidden and the
    // paragraph must be left exactly as it would be with no limit at all.
    return FALSE;
  }

  // The bottom edge of the last line that stays, taken from the layout's own
  // iterator rather than by multiplying one line's height by the limit: a
  // paragraph whose fragments set different fontSizes or lineHeights has lines
  // of different heights, and that arithmetic would land part way into a glyph.
  PangoLayoutIter *iter = pango_layout_get_iter(layout);
  int line_top = 0;
  int line_bottom = 0;
  for (int line = 0; line < limit; line++) {
    pango_layout_iter_get_line_yrange(iter, &line_top, &line_bottom);
    if (!pango_layout_iter_next_line(iter)) {
      break;
    }
  }
  pango_layout_iter_free(iter);

  // Pango answers in 1/1024ths of a pixel, and everything above this line is in
  // React Native's points.
  *out_height = static_cast<float>(line_bottom) / PANGO_SCALE;
  return TRUE;
}

void rn_view_set_role_name(RnView *self, const char *name) {
  g_return_if_fail(RN_IS_VIEW(self));
  g_free(self->role_name);
  self->role_name = name != nullptr && *name != '\0' ? g_strdup(name) : nullptr;
}

void rn_view_set_test_id(RnView *self, const char *test_id) {
  g_return_if_fail(RN_IS_VIEW(self));
  g_free(self->test_id);
  self->test_id = test_id != nullptr && *test_id != '\0' ? g_strdup(test_id) : nullptr;
}

void rn_view_set_native_id(RnView *self, const char *native_id) {
  g_return_if_fail(RN_IS_VIEW(self));
  g_free(self->native_id);
  // Empty becomes NULL, as the role name above does: React Native sends "" for
  // a view with no nativeID at all, and storing that would make every
  // unmarked view compare equal to an empty marker rather than to nothing.
  self->native_id = native_id != nullptr && *native_id != '\0' ? g_strdup(native_id) : nullptr;
}

const char *rn_view_get_native_id(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);
  return self->native_id;
}

void rn_view_set_cursor(RnView *self, const char *name) {
  g_return_if_fail(RN_IS_VIEW(self));

  const char *wanted = name != nullptr && *name != '\0' ? name : nullptr;
  if (g_strcmp0(self->cursor_name, wanted) == 0) {
    return;
  }
  g_free(self->cursor_name);
  self->cursor_name = wanted != nullptr ? g_strdup(wanted) : nullptr;
  // NULL unsets it, and unset means inherited rather than arrow: a view inside
  // one that asked for a hand keeps the hand, which is what CSS does and what
  // makes `cursor` worth setting on a container at all.
  gtk_widget_set_cursor_from_name(GTK_WIDGET(self), self->cursor_name);
}

const char *rn_view_get_cursor(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);
  return self->cursor_name;
}

// `mixBlendMode`, as the CSS keyword, mapped onto GSK's own list.
//
// GSK has a mode per CSS mode bar one: `plus-lighter`, which CSS defines as
// clamped additive compositing and GSK has no node for. A keyword GSK cannot do
// leaves the view unblended, and the keyword is still stored and still reported,
// so the dump says what the app asked for -- the same rule `cursor` follows for
// a name no theme has.
static gboolean rn_blend_mode_from_name(const char *name, GskBlendMode *out) {
  static const struct {
    const char *name;
    GskBlendMode mode;
  } kModes[] = {
      {"multiply", GSK_BLEND_MODE_MULTIPLY},
      {"screen", GSK_BLEND_MODE_SCREEN},
      {"overlay", GSK_BLEND_MODE_OVERLAY},
      {"darken", GSK_BLEND_MODE_DARKEN},
      {"lighten", GSK_BLEND_MODE_LIGHTEN},
      {"color-dodge", GSK_BLEND_MODE_COLOR_DODGE},
      {"color-burn", GSK_BLEND_MODE_COLOR_BURN},
      {"hard-light", GSK_BLEND_MODE_HARD_LIGHT},
      {"soft-light", GSK_BLEND_MODE_SOFT_LIGHT},
      {"difference", GSK_BLEND_MODE_DIFFERENCE},
      {"exclusion", GSK_BLEND_MODE_EXCLUSION},
      {"hue", GSK_BLEND_MODE_HUE},
      {"saturation", GSK_BLEND_MODE_SATURATION},
      {"color", GSK_BLEND_MODE_COLOR},
      {"luminosity", GSK_BLEND_MODE_LUMINOSITY},
  };
  for (gsize i = 0; i < G_N_ELEMENTS(kModes); i++) {
    if (g_strcmp0(name, kModes[i].name) == 0) {
      *out = kModes[i].mode;
      return TRUE;
    }
  }
  return FALSE;
}

void rn_view_set_blend_mode(RnView *self, const char *name) {
  g_return_if_fail(RN_IS_VIEW(self));

  const char *wanted = name != nullptr && *name != '\0' ? name : nullptr;
  if (g_strcmp0(self->blend_name, wanted) == 0) {
    return;
  }
  g_free(self->blend_name);
  self->blend_name = wanted != nullptr ? g_strdup(wanted) : nullptr;
  self->blends = wanted != nullptr && rn_blend_mode_from_name(wanted, &self->blend_mode);
  if (!self->blends) {
    self->blend_mode = GSK_BLEND_MODE_DEFAULT;
  }
  // The parent paints the blend, not this view: a blend needs the backdrop, and
  // this view cannot see it. So it is the parent that has to redraw.
  GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(self));
  gtk_widget_queue_draw(parent != nullptr ? parent : GTK_WIDGET(self));
}

const char *rn_view_get_blend_mode(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);
  return self->blend_name;
}

// The spelling React Native uses for the prop, which is also CSS's, so the
// three hosts' dumps say the same words.
static const char *rn_pointer_events_name(RnPointerEvents mode) {
  switch (mode) {
    case RN_POINTER_EVENTS_NONE:
      return "none";
    case RN_POINTER_EVENTS_BOX_NONE:
      return "box-none";
    case RN_POINTER_EVENTS_BOX_ONLY:
      return "box-only";
    case RN_POINTER_EVENTS_AUTO:
      break;
  }
  return "auto";
}

static const char *rn_image_fit_name(RnImageFit fit) {
  switch (fit) {
    case RN_IMAGE_FIT_CONTAIN:
      return "contain";
    case RN_IMAGE_FIT_STRETCH:
      return "stretch";
    case RN_IMAGE_FIT_CENTER:
      return "center";
    case RN_IMAGE_FIT_REPEAT:
      return "repeat";
    case RN_IMAGE_FIT_COVER:
      break;
  }
  return "cover";
}

void rn_view_set_image_tint(RnView *self, gboolean has_tint, const GdkRGBA *tint) {
  if (!RN_IS_VIEW(self)) {
    return;
  }
  const gboolean changed = self->has_image_tint != has_tint ||
      (has_tint && tint != nullptr && !gdk_rgba_equal(&self->image_tint, tint));
  self->has_image_tint = has_tint;
  if (has_tint && tint != nullptr) {
    self->image_tint = *tint;
  }
  if (changed) {
    gtk_widget_queue_draw(GTK_WIDGET(self));
  }
}

void rn_view_set_texture(RnView *self, GdkTexture *texture, RnImageFit fit) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (texture != nullptr) {
    g_object_ref(texture);
  }
  g_clear_object(&self->texture);
  self->texture = texture;
  self->texture_fit = fit;

  gtk_widget_queue_draw(GTK_WIDGET(self));
}

// Shows whatever frame the iterator is on, as a texture.
//
// A new GdkTexture per frame, which sounds wasteful and is what GDK offers: a
// texture is immutable, and gdk-pixbuf hands back a GdkPixbuf. The frames are
// small (an animated image is an icon or a spinner, in practice) and the
// alternative is a GL upload path this host does not have.
// gdk-pixbuf 2.44 deprecated its whole animation API, with nothing in
// gdk-pixbuf to replace it: GNOME's direction is glycin, a separate library
// and a new dependency, and GTK4 itself has no frame source for a GIF at all
// -- `gtk_image_set_from_file` on one shows a still. So this uses the
// deprecated API deliberately rather than by accident, and
// docs/backlog/image.md records what replacing it would take.
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

static void rn_view_show_animation_frame(RnView *self) {
  GdkPixbuf *pixbuf = gdk_pixbuf_animation_iter_get_pixbuf(self->animation_iter);
  if (pixbuf == nullptr) {
    return;
  }
  GdkTexture *texture = gdk_texture_new_for_pixbuf(pixbuf);
  if (texture == nullptr) {
    return;
  }
  rn_view_set_texture(self, texture, self->texture_fit);
  g_object_unref(texture);
}

// The current frame's delay, clamped the way both hosts clamp it.
static double rn_view_animation_delay_ms(RnView *self) {
  const int delay = gdk_pixbuf_animation_iter_get_delay_time(self->animation_iter);
  if (delay < 0) {
    // gdk-pixbuf's answer for "this frame is the last one", which a GIF that
    // does not loop reaches.
    return 0.0;
  }
  return basalt::clampedImageFrameDelay(static_cast<unsigned>(delay));
}

double rn_view_advance_animation(RnView *self, double milliseconds) {
  g_return_val_if_fail(RN_IS_VIEW(self), 0.0);
  if (self->animation_iter == nullptr) {
    return 0.0;
  }

  self->animation_budget_ms += milliseconds;

  // One frame at a time, and the iterator is advanced by *its* delay while the
  // budget is spent at *ours*. So gdk-pixbuf decides where the frame boundaries
  // are and this decides when they are reached, which is how a GIF asking for a
  // delay of zero is paced the same here as on the other host: the clamp is
  // shared and the frames are not.
  //
  // Frame by frame rather than by jumping, because the iterator only moves
  // forward and has no seek. The work is proportional to the time advanced,
  // which is a frame clock's delta in an app and a number a test chose in the
  // suite.
  for (;;) {
    const double delay = rn_view_animation_delay_ms(self);
    if (delay <= 0.0) {
      // The last frame of an animation that does not loop. Nothing more is due,
      // which is what the caller stops ticking on.
      self->animation_budget_ms = 0.0;
      return 0.0;
    }
    if (self->animation_budget_ms < delay) {
      return delay - self->animation_budget_ms;
    }
    self->animation_budget_ms -= delay;

    const int own = gdk_pixbuf_animation_iter_get_delay_time(self->animation_iter);
    self->animation_at_us += static_cast<gint64>(own > 0 ? own : 0) * 1000;
    GTimeVal at;
    at.tv_sec = static_cast<glong>(self->animation_at_us / G_USEC_PER_SEC);
    at.tv_usec = static_cast<glong>(self->animation_at_us % G_USEC_PER_SEC);
    if (gdk_pixbuf_animation_iter_advance(self->animation_iter, &at)) {
      rn_view_show_animation_frame(self);
    }
  }
}

static gboolean rn_view_animation_tick(GtkWidget *widget,
                                       GdkFrameClock *clock,
                                       gpointer data) {
  RnView *self = RN_VIEW(widget);
  (void)data;

  if (self->animation_iter == nullptr) {
    self->animation_tick = 0;
    return G_SOURCE_REMOVE;
  }

  // The frame clock's own timestamps, so the advance is by the time that
  // actually passed: a 120Hz display and a 60Hz one then run the animation at
  // the same speed, and a window the compositor stopped drawing does not
  // accumulate time it never showed.
  const gint64 now = gdk_frame_clock_get_frame_time(clock);
  const gint64 since = self->animation_tick_at_us == 0 ? 0 : now - self->animation_tick_at_us;
  self->animation_tick_at_us = now;

  if (rn_view_advance_animation(self, static_cast<double>(since) / 1000.0) <= 0.0) {
    self->animation_tick = 0;
    return G_SOURCE_REMOVE;
  }
  return G_SOURCE_CONTINUE;
}

void rn_view_set_animation(RnView *self, GdkPixbufAnimation *animation) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (self->animation == animation) {
    // The same animation again, which is what a layout-only mutation produces:
    // the loader hands back the object it cached, so this is a pointer compare
    // and the animation keeps its place rather than starting over on a resize.
    return;
  }

  if (self->animation_tick != 0) {
    gtk_widget_remove_tick_callback(GTK_WIDGET(self), self->animation_tick);
    self->animation_tick = 0;
  }
  g_clear_object(&self->animation_iter);
  g_clear_object(&self->animation);
  self->animation_at_us = 0;
  self->animation_budget_ms = 0.0;
  self->animation_tick_at_us = 0;

  if (animation == nullptr || gdk_pixbuf_animation_is_static_image(animation)) {
    return;
  }

  self->animation = static_cast<GdkPixbufAnimation *>(g_object_ref(animation));
  GTimeVal start;
  start.tv_sec = 0;
  start.tv_usec = 0;
  self->animation_iter = gdk_pixbuf_animation_get_iter(self->animation, &start);
  rn_view_show_animation_frame(self);
}

void rn_view_start_animation(RnView *self) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->animation_iter == nullptr || self->animation_tick != 0) {
    return;
  }

  // A tick callback rather than a timeout, so the frames arrive with the
  // compositor's and a window that is not being drawn is not woken. It asks
  // for every frame and advances by the time that passed, which is cheap next
  // to redrawing: the advance only produces a texture when a delay has
  // actually elapsed.
  self->animation_tick =
      gtk_widget_add_tick_callback(GTK_WIDGET(self), rn_view_animation_tick, nullptr, nullptr);
}

gboolean rn_view_is_animated(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), FALSE);
  return self->animation != nullptr;
}

G_GNUC_END_IGNORE_DEPRECATIONS

static void rn_view_scroll_offset(RnView *self, double *offset_x, double *offset_y) {
  *offset_x = self->scroll_x;
  *offset_y = self->scroll_y;
}

static gboolean rn_view_layout_transform(RnView *self, graphene_matrix_t *out) {
  if (!self->has_transform) {
    return FALSE;
  }
  graphene_matrix_init_from_matrix(out, &self->transform);
  return TRUE;
}

static int rn_view_layout_z_index(RnView *self) {
  return self->z_index;
}

void rn_view_set_border_radii(RnView *self, const graphene_size_t radii[4]) {
  g_return_if_fail(RN_IS_VIEW(self));

  gboolean any = FALSE;
  for (int i = 0; i < 4; i++) {
    self->border_radii[i] = radii != nullptr ? radii[i] : graphene_size_t{0.0f, 0.0f};
    if (self->border_radii[i].width > 0.0f || self->border_radii[i].height > 0.0f) {
      any = TRUE;
    }
  }
  self->has_border_radii = any;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_gradients(RnView *self, const RnGradient *gradients, int count) {
  g_return_if_fail(RN_IS_VIEW(self));

  const guint wanted = gradients != nullptr && count > 0 ? static_cast<guint>(count) : 0;
  const guint had = self->gradients != nullptr ? self->gradients->len : 0;
  if (wanted == 0 && had == 0) {
    return;
  }

  // Compared before it is replaced, for the reason the shadows are: a view
  // re-sends identical props on every mutation, and a window repainted per
  // mutation is a gradient recomputed for nothing.
  if (wanted == had && wanted > 0) {
    gboolean same = TRUE;
    for (guint i = 0; i < wanted && same; i++) {
      const RnGradientRecord &have = g_array_index(self->gradients, RnGradientRecord, i);
      const RnGradient &want = gradients[i];
      same = have.kind == want.kind && graphene_point_equal(&have.start, &want.start) &&
             graphene_point_equal(&have.end, &want.end) &&
             graphene_point_equal(&have.center, &want.center) &&
             have.radius_x == want.radius_x && have.radius_y == want.radius_y &&
             graphene_rect_equal(&have.area, &want.area) &&
             graphene_rect_equal(&have.tile, &want.tile) && have.repeats == want.repeats &&
             have.stops->len == static_cast<guint>(want.stop_count) &&
             memcmp(have.stops->data,
                    want.stops,
                    have.stops->len * sizeof(RnGradientStop)) == 0;
    }
    if (same) {
      return;
    }
  }

  if (wanted == 0) {
    g_clear_pointer(&self->gradients, g_array_unref);
    gtk_widget_queue_draw(GTK_WIDGET(self));
    return;
  }

  if (self->gradients == nullptr) {
    self->gradients = g_array_sized_new(FALSE, FALSE, sizeof(RnGradientRecord), wanted);
    g_array_set_clear_func(self->gradients, rn_gradient_record_clear);
  } else {
    g_array_set_size(self->gradients, 0);
  }
  for (guint i = 0; i < wanted; i++) {
    const RnGradient &gradient = gradients[i];
    RnGradientRecord record{gradient.kind,
                            gradient.start,
                            gradient.end,
                            gradient.center,
                            gradient.radius_x,
                            gradient.radius_y,
                            gradient.area,
                            gradient.tile,
                            gradient.repeats,
                            nullptr};
    const guint stops = gradient.stop_count > 0 ? static_cast<guint>(gradient.stop_count) : 0;
    record.stops = g_array_sized_new(FALSE, FALSE, sizeof(RnGradientStop), MAX(stops, 1));
    if (stops > 0) {
      g_array_append_vals(record.stops, gradient.stops, stops);
    }
    g_array_append_val(self->gradients, record);
  }
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

int rn_view_get_gradient_count(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), 0);
  return self->gradients != nullptr ? static_cast<int>(self->gradients->len) : 0;
}

void rn_view_set_box_shadows(RnView *self, const RnBoxShadow *shadows, int count) {
  g_return_if_fail(RN_IS_VIEW(self));

  const guint wanted = shadows != nullptr && count > 0 ? static_cast<guint>(count) : 0;
  const guint had = self->box_shadows != nullptr ? self->box_shadows->len : 0;
  if (wanted == 0 && had == 0) {
    return;
  }
  // Compared rather than replaced blindly: a controlled view re-sends identical
  // props on every keystroke, and queueing a draw per mutation would repaint the
  // window for nothing.
  if (wanted == had && wanted > 0 &&
      memcmp(self->box_shadows->data, shadows, wanted * sizeof(RnBoxShadow)) == 0) {
    return;
  }

  if (wanted == 0) {
    g_clear_pointer(&self->box_shadows, g_array_unref);
  } else {
    if (self->box_shadows == nullptr) {
      self->box_shadows = g_array_sized_new(FALSE, FALSE, sizeof(RnBoxShadow), wanted);
    } else {
      g_array_set_size(self->box_shadows, 0);
    }
    g_array_append_vals(self->box_shadows, shadows, wanted);
  }
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

int rn_view_get_box_shadow_count(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), 0);
  return self->box_shadows != nullptr ? static_cast<int>(self->box_shadows->len) : 0;
}

void rn_view_set_outline(RnView *self,
                         float width,
                         float offset,
                         const GdkRGBA *color,
                         RnBorderStyle style) {
  g_return_if_fail(RN_IS_VIEW(self));

  const float wanted = width > 0.0f ? width : 0.0f;
  const GdkRGBA wantedColor = color != nullptr ? *color : GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  if (self->outline_width == wanted && self->outline_offset == offset &&
      self->outline_style == style && gdk_rgba_equal(&self->outline_color, &wantedColor)) {
    return;
  }
  self->outline_width = wanted;
  self->outline_offset = offset;
  self->outline_color = wantedColor;
  self->outline_style = style;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_filters(RnView *self, const RnFilters *filters) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (filters == nullptr) {
    if (!self->has_filters) {
      return;
    }
    self->has_filters = FALSE;
    g_clear_pointer(&self->filter_shadows, g_array_unref);
    gtk_widget_queue_draw(GTK_WIDGET(self));
    return;
  }

  // The scalar half is compared with the borrowed pointer taken out of it: a
  // pointer into the caller's vector is not a value this widget can keep, and
  // the shadows themselves are compared beside it. A view re-sends identical
  // props on every mutation, so this is what stops a redraw per mutation.
  RnFilters wanted = *filters;
  wanted.shadows = nullptr;
  wanted.shadow_count = 0;
  const guint wantedShadows =
      filters->shadows != nullptr && filters->shadow_count > 0
      ? static_cast<guint>(filters->shadow_count)
      : 0;
  const guint haveShadows =
      self->filter_shadows != nullptr ? self->filter_shadows->len : 0;
  if (self->has_filters && memcmp(&self->filters, &wanted, sizeof(RnFilters)) == 0 &&
      wantedShadows == haveShadows &&
      (wantedShadows == 0 ||
       memcmp(self->filter_shadows->data,
              filters->shadows,
              wantedShadows * sizeof(RnFilterShadow)) == 0)) {
    return;
  }

  self->filters = wanted;
  self->has_filters = TRUE;
  if (wantedShadows == 0) {
    g_clear_pointer(&self->filter_shadows, g_array_unref);
  } else {
    if (self->filter_shadows == nullptr) {
      self->filter_shadows =
          g_array_sized_new(FALSE, FALSE, sizeof(RnFilterShadow), wantedShadows);
    } else {
      g_array_set_size(self->filter_shadows, 0);
    }
    g_array_append_vals(self->filter_shadows, filters->shadows, wantedShadows);
  }
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_hit_slop(RnView *self, const float insets[4]) {
  g_return_if_fail(RN_IS_VIEW(self));

  gboolean any = FALSE;
  for (int edge = 0; edge < 4; edge++) {
    self->hit_slop[edge] = insets != nullptr ? insets[edge] : 0.0f;
    if (self->hit_slop[edge] != 0.0f) {
      any = TRUE;
    }
  }
  self->has_hit_slop = any;
  // Nothing to queue: the prop moves no pixels. Picking asks `contains` fresh
  // on every event.
}

void rn_view_set_border_style(RnView *self, RnBorderStyle style) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (self->border_style == style) {
    return;
  }
  self->border_style = style;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_borders(RnView *self, const float widths[4], const GdkRGBA colors[4]) {
  g_return_if_fail(RN_IS_VIEW(self));

  gboolean any = FALSE;
  for (int i = 0; i < 4; i++) {
    self->border_widths[i] = widths != nullptr ? widths[i] : 0.0f;
    self->border_colors[i] = colors != nullptr ? colors[i] : GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
    if (self->border_widths[i] > 0.0f && self->border_colors[i].alpha > 0.0f) {
      any = TRUE;
    }
  }
  self->has_borders = any;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

// Hidden through `gtk_widget_set_visible` rather than by skipping the
// snapshot: it takes the children with it, which is what a back face should
// do, and it takes hit testing with it too, since `gtk_widget_pick` skips a
// widget that is not visible.
//
// The two reasons are kept apart and combined here. Visibility is one flag and
// `display: none` writes it too, so whichever of the two spoke last used to
// answer for both -- and layout metrics are applied after props, so a card
// turned away from the viewer was reliably shown again a moment later.
static void rn_view_apply_visibility(RnView *self) {
  gtk_widget_set_visible(GTK_WIDGET(self),
                         (self->hidden_by_app || self->hidden_by_back_face) ? FALSE : TRUE);
}

void rn_view_set_hidden(RnView *self, gboolean hidden) {
  g_return_if_fail(RN_IS_VIEW(self));
  self->hidden_by_app = hidden;
  rn_view_apply_visibility(self);
}

static void rn_view_update_back_face(RnView *self) {
  gboolean away = FALSE;
  if (self->hides_back_face) {
    float values[16];
    graphene_matrix_to_float(&self->transform, values);
    away = basalt::facesAway(values) ? TRUE : FALSE;
  }
  // Recomputed rather than returned early on, so that turning the prop off
  // shows a view it had hidden instead of leaving it hidden forever.
  self->hidden_by_back_face = away;
  rn_view_apply_visibility(self);
}

void rn_view_set_transform(RnView *self, const graphene_matrix_t *matrix) {
  g_return_if_fail(RN_IS_VIEW(self));

  if (matrix == nullptr) {
    graphene_matrix_init_identity(&self->transform);
    self->has_transform = FALSE;
  } else {
    graphene_matrix_init_from_matrix(&self->transform, matrix);
    self->has_transform = !graphene_matrix_is_identity(matrix);
  }
  rn_view_update_back_face(self);
  // Composed during the *parent's* allocation, alongside the frame, so it is
  // the parent that has to be redone. Queueing on this widget leaves a cleared
  // transform still applied until something else moves the parent.
  GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(self));
  gtk_widget_queue_allocate(parent != nullptr ? parent : GTK_WIDGET(self));
}

void rn_view_set_hides_back_face(RnView *self, gboolean hides) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->hides_back_face == hides) {
    return;
  }
  self->hides_back_face = hides;
  rn_view_update_back_face(self);
}

void rn_view_set_z_index(RnView *self, int z_index) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->z_index == z_index) {
    return;
  }
  self->z_index = z_index;
  GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(self));
  if (parent != nullptr) {
    gtk_widget_queue_draw(parent);
  }
}

void rn_view_set_pointer_events(RnView *self, RnPointerEvents mode) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->pointer_events == mode) {
    return;
  }
  self->pointer_events = mode;
  // `none` is the one mode GTK understands: an untargetable widget is skipped
  // by gtk_widget_pick along with everything inside it, so a press lands on
  // whatever is behind. The other two are left to the hit test, because a
  // widget can only be targetable or not and they are neither.
  gtk_widget_set_can_target(GTK_WIDGET(self), mode != RN_POINTER_EVENTS_NONE);
}

RnPointerEvents rn_view_get_pointer_events(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), RN_POINTER_EVENTS_AUTO);
  return self->pointer_events;
}

void rn_view_set_focusable(RnView *self, gboolean focusable) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->focusable == focusable) {
    return;
  }
  self->focusable = focusable;
  // `focusable` is whether the widget can hold focus at all; `can-focus` is
  // whether Tab will give it any. GTK wants both, and a widget with only the
  // first is reachable by a click and never by the keyboard.
  gtk_widget_set_focusable(GTK_WIDGET(self), focusable);
  gtk_widget_set_can_focus(GTK_WIDGET(self), focusable);
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

gboolean rn_view_get_focusable(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), FALSE);
  return self->focusable;
}

void rn_view_set_clips_children(RnView *self, gboolean clips) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->clips_children == clips) {
    return;
  }
  self->clips_children = clips;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_scroll_indicator_colour(RnView *self,
                                         float red,
                                         float green,
                                         float blue,
                                         float alpha) {
  if (!RN_IS_VIEW(self)) {
    return;
  }
  const GdkRGBA wanted{red, green, blue, alpha};
  if (gdk_rgba_equal(&self->indicator_colour, &wanted)) {
    return;
  }
  self->indicator_colour = wanted;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_scroll_indicators(RnView *self,
                                   double vertical_offset,
                                   double vertical_length,
                                   double horizontal_offset,
                                   double horizontal_length) {
  if (!RN_IS_VIEW(self)) {
    return;
  }
  if (self->indicator_v_offset == vertical_offset && self->indicator_v_length == vertical_length &&
      self->indicator_h_offset == horizontal_offset &&
      self->indicator_h_length == horizontal_length) {
    return;
  }
  self->indicator_v_offset = vertical_offset;
  self->indicator_v_length = vertical_length;
  self->indicator_h_offset = horizontal_offset;
  self->indicator_h_length = horizontal_length;
  gtk_widget_queue_draw(GTK_WIDGET(self));
}

void rn_view_set_scroll_offset(RnView *self, double offset_x, double offset_y) {
  g_return_if_fail(RN_IS_VIEW(self));
  if (self->scroll_x == offset_x && self->scroll_y == offset_y) {
    return;
  }
  self->scroll_x = offset_x;
  self->scroll_y = offset_y;
  // Children move, so the layout has to run again; a redraw alone would leave
  // them where they were.
  gtk_widget_queue_allocate(GTK_WIDGET(self));
}

void rn_view_get_scroll_offset(RnView *self, double *offset_x, double *offset_y) {
  g_return_if_fail(RN_IS_VIEW(self));
  rn_view_scroll_offset(self, offset_x, offset_y);
}

// Escaping for the tree dump: backslash, quote and newline, and nothing else.
//
// Not g_strescape, which also turns every byte above 0x7f into an octal escape
// -- so a string with a "·" in it comes out as \302\267 on this side and as
// itself on the AppKit side, and the two dumps differ on a character neither
// platform did anything to. The result is also simply easier to read.
static char *rn_escape_for_dump(const char *text) {
  GString *escaped = g_string_new(nullptr);
  for (const char *c = text; c != nullptr && *c != '\0'; c++) {
    switch (*c) {
      case '\\':
        g_string_append(escaped, "\\\\");
        break;
      case '"':
        g_string_append(escaped, "\\\"");
        break;
      case '\n':
        g_string_append(escaped, "\\n");
        break;
      default:
        g_string_append_c(escaped, *c);
        break;
    }
  }
  return g_string_free(escaped, FALSE);
}

static void rn_view_describe_into(RnView *self, GString *out, int depth) {
  for (int i = 0; i < depth; i++) {
    g_string_append(out, "  ");
  }

  g_string_append_printf(out,
                         "view tag=%d frame=(%g,%g %gx%g)",
                         self->tag,
                         static_cast<double>(self->frame.origin.x),
                         static_cast<double>(self->frame.origin.y),
                         static_cast<double>(self->frame.size.width),
                         static_cast<double>(self->frame.size.height));

  if (self->has_background_color) {
    g_string_append_printf(out,
                           " bg=#%02x%02x%02x%02x",
                           static_cast<unsigned>(self->background_color.red * 255.0 + 0.5),
                           static_cast<unsigned>(self->background_color.green * 255.0 + 0.5),
                           static_cast<unsigned>(self->background_color.blue * 255.0 + 0.5),
                           static_cast<unsigned>(self->background_color.alpha * 255.0 + 0.5));
  }
  if (self->opacity < 1.0) {
    g_string_append_printf(out, " opacity=%g", self->opacity);
  }
  if (self->clips_children) {
    g_string_append(out, " clip");
  }
  // Whether anything is drawn at all. Without this a view hidden by
  // `display: none` or by a back face turned away reads exactly like a visible
  // one, and the only prop in this dump that removes a view entirely was the
  // only one it could not show.
  if (!gtk_widget_get_visible(GTK_WIDGET(self))) {
    g_string_append(out, " hidden");
  }
  // Per-corner radii and per-edge borders. These are in the dump for the same
  // reason `transform=` is: a frame cannot show them, so a view that is
  // bordered on one desktop and bare on another reads as identical here. That
  // is exactly how the macOS host went this long applying neither.
  //
  // Radii are eight numbers rather than four because React Native's are
  // elliptical -- a horizontal and a vertical radius per corner -- in the order
  // top-left, top-right, bottom-right, bottom-left.
  if (self->has_border_radii) {
    g_string_append_printf(out,
                           " radii=(%g,%g,%g,%g,%g,%g,%g,%g)",
                           static_cast<double>(self->border_radii[0].width),
                           static_cast<double>(self->border_radii[0].height),
                           static_cast<double>(self->border_radii[1].width),
                           static_cast<double>(self->border_radii[1].height),
                           static_cast<double>(self->border_radii[2].width),
                           static_cast<double>(self->border_radii[2].height),
                           static_cast<double>(self->border_radii[3].width),
                           static_cast<double>(self->border_radii[3].height));
  }
  // Widths and colours are top, right, bottom, left -- the order CSS names
  // them, and the order every platform here stores them in.
  if (self->has_borders) {
    g_string_append_printf(out,
                           " borderw=(%g,%g,%g,%g)",
                           static_cast<double>(self->border_widths[0]),
                           static_cast<double>(self->border_widths[1]),
                           static_cast<double>(self->border_widths[2]),
                           static_cast<double>(self->border_widths[3]));
    g_string_append(out, " borderc=(");
    for (int i = 0; i < 4; i++) {
      g_string_append_printf(out,
                             "%s#%02x%02x%02x%02x",
                             i == 0 ? "" : ",",
                             static_cast<unsigned>(self->border_colors[i].red * 255.0 + 0.5),
                             static_cast<unsigned>(self->border_colors[i].green * 255.0 + 0.5),
                             static_cast<unsigned>(self->border_colors[i].blue * 255.0 + 0.5),
                             static_cast<unsigned>(self->border_colors[i].alpha * 255.0 + 0.5));
    }
    g_string_append(out, ")");
    // The style, when it is not solid. Printed for the same reason the widths
    // are: a dashed border and a solid one are the same four widths and the same
    // four colours, and this is the only thing that can say the prop arrived.
    if (self->border_style != RN_BORDER_SOLID) {
      g_string_append_printf(
          out, " border-style=%s", self->border_style == RN_BORDER_DOTTED ? "dotted" : "dashed");
    }
  }
  // Gradients: how many, each one's line and stop count, where the image goes
  // and the tile it repeats in. Not every stop, which a transition hint can turn
  // into eleven of: what a cross-host diff and an end-to-end run need is that
  // the same gradient arrived with the same geometry, and the stop fixup itself
  // is asserted in core's own tests.
  //
  // `at=` is the rectangle the image fills and `tile=` is the period, printed
  // only when it repeats -- so a `no-repeat` background is the line without a
  // tile. The three background props are invisible in every other line of this
  // dump: they move and repeat the image without changing the view at all.
  if (self->gradients != nullptr) {
    for (guint i = 0; i < self->gradients->len; i++) {
      const RnGradientRecord &gradient = g_array_index(self->gradients, RnGradientRecord, i);
      if (gradient.kind == RN_GRADIENT_RADIAL) {
        g_string_append_printf(out,
                               " gradient=(radial (%g,%g) %gx%g,%u stops",
                               static_cast<double>(gradient.center.x),
                               static_cast<double>(gradient.center.y),
                               static_cast<double>(gradient.radius_x),
                               static_cast<double>(gradient.radius_y),
                               gradient.stops != nullptr ? gradient.stops->len : 0);
        rn_view_describe_gradient_layer(out, gradient);
      } else {
        g_string_append_printf(out,
                               " gradient=((%g,%g)-(%g,%g),%u stops",
                               static_cast<double>(gradient.start.x),
                               static_cast<double>(gradient.start.y),
                               static_cast<double>(gradient.end.x),
                               static_cast<double>(gradient.end.y),
                               gradient.stops != nullptr ? gradient.stops->len : 0);
        rn_view_describe_gradient_layer(out, gradient);
      }
    }
  }

  // Box shadows, each in full. Nothing else in this dump can say a shadow is
  // there, and the numbers are the whole feature: an offset that went to the
  // wrong axis or a spread read as a blur still draws a plausible shadow.
  if (self->box_shadows != nullptr) {
    for (guint i = 0; i < self->box_shadows->len; i++) {
      const RnBoxShadow &shadow = g_array_index(self->box_shadows, RnBoxShadow, i);
      g_string_append_printf(out,
                             " shadow=(%s%g,%g,%g,%g,#%02x%02x%02x%02x)",
                             shadow.inset ? "inset " : "",
                             static_cast<double>(shadow.dx),
                             static_cast<double>(shadow.dy),
                             static_cast<double>(shadow.blur),
                             static_cast<double>(shadow.spread),
                             static_cast<unsigned>(shadow.color.red * 255.0 + 0.5),
                             static_cast<unsigned>(shadow.color.green * 255.0 + 0.5),
                             static_cast<unsigned>(shadow.color.blue * 255.0 + 0.5),
                             static_cast<unsigned>(shadow.color.alpha * 255.0 + 0.5));
    }
  }
  if (self->has_transform) {
    // The 2D affine part, in the order CSS writes a matrix(): a, b, c, d, tx,
    // ty. The AppKit side prints the same six from its CATransform3D. See the
    // comment there for why this is in the dump at all.
    float values[16];
    graphene_matrix_to_float(&self->transform, values);
    g_string_append_printf(out,
                           " transform=(%g,%g,%g,%g,%g,%g)",
                           values[0],
                           values[1],
                           values[4],
                           values[5],
                           values[12],
                           values[13]);
  }
  if (self->scroll_x != 0.0 || self->scroll_y != 0.0) {
    g_string_append_printf(out, " scroll=(%g,%g)", self->scroll_x, self->scroll_y);
  }
  // The overlay scrollbars, which are otherwise pure paint and so invisible to
  // every test this project has. Printed only when there is one, so a view that
  // does not scroll stays as short as it was.
  if (self->indicator_v_length > 0.0) {
    g_string_append_printf(
        out, " scrollbar-v=(%g,%g)", self->indicator_v_offset, self->indicator_v_length);
  }
  if (self->indicator_h_length > 0.0) {
    g_string_append_printf(
        out, " scrollbar-h=(%g,%g)", self->indicator_h_offset, self->indicator_h_length);
  }
  // The thumb's colour, printed only when it is not the default black: a white
  // one is `indicatorStyle` having arrived, and the colour is the only part of
  // that the cross-host diff can see. Pure paint otherwise, like the two above.
  if (self->indicator_colour.red != 0.0F || self->indicator_colour.green != 0.0F ||
      self->indicator_colour.blue != 0.0F) {
    g_string_append_printf(out,
                           " scrollbar-colour=(%g,%g,%g,%g)",
                           static_cast<double>(self->indicator_colour.red),
                           static_cast<double>(self->indicator_colour.green),
                           static_cast<double>(self->indicator_colour.blue),
                           static_cast<double>(self->indicator_colour.alpha));
  }
  // Printed only when it is not the default, like every other field here.
  // Worth printing at all because it is invisible: a view with
  // `pointerEvents: none` is drawn exactly like one without, and the only way
  // the cross-host diff can say the prop arrived on all three is if each one
  // reports it.
  if (self->pointer_events != RN_POINTER_EVENTS_AUTO) {
    g_string_append_printf(out, " pe=%s", rn_pointer_events_name(self->pointer_events));
  }
  // Printed for the reason pointerEvents is: the prop is invisible in a still
  // picture, so a dump is the only thing that can say it arrived on both hosts.
  if (self->cursor_name != nullptr) {
    g_string_append_printf(out, " cursor=%s", self->cursor_name);
  }
  // The blend mode the app asked for, which is also invisible in a frame unless
  // something is beneath it. The keyword rather than the GSK mode, so the dump
  // is comparable with the other host's.
  if (self->blend_name != nullptr) {
    g_string_append_printf(out, " blend=%s", self->blend_name);
  }
  // The outline, which is invisible in every other line: it is not a border and
  // a view with one has the same frame and the same colours without it.
  if (self->outline_width > 0.0f) {
    g_string_append_printf(out,
                           " outline=(%g,%g,#%02x%02x%02x%02x",
                           static_cast<double>(self->outline_width),
                           static_cast<double>(self->outline_offset),
                           static_cast<unsigned>(self->outline_color.red * 255.0 + 0.5),
                           static_cast<unsigned>(self->outline_color.green * 255.0 + 0.5),
                           static_cast<unsigned>(self->outline_color.blue * 255.0 + 0.5),
                           static_cast<unsigned>(self->outline_color.alpha * 255.0 + 0.5));
    if (self->outline_style != RN_BORDER_SOLID) {
      g_string_append_printf(
          out, ",%s", self->outline_style == RN_BORDER_DOTTED ? "dotted" : "dashed");
    }
    g_string_append(out, ")");
  }

  // `filter`, as the pieces it came to plus what its matrix makes of one probe
  // colour. Sixteen numbers would drown the line; one colour is eight characters
  // and still fails when a matrix is wrong, which is what the cross-host diff
  // and the end-to-end run need. The probe is (1, 0.5, 0.25) so that no two
  // channels can be swapped without the answer changing.
  if (self->has_filters) {
    g_string_append(out, " filter=(");
    gboolean first = TRUE;
    if (self->filters.has_matrix) {
      const float probe[4] = {1.0f, 0.5f, 0.25f, 1.0f};
      float result[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      for (int row = 0; row < 4; row++) {
        result[row] = self->filters.offset[row];
        for (int column = 0; column < 4; column++) {
          result[row] += self->filters.matrix[row * 4 + column] * probe[column];
        }
      }
      const auto byte = [](float value) {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<unsigned>(clamped * 255.0f + 0.5f);
      };
      g_string_append_printf(out,
                             "probe=#%02x%02x%02x%02x",
                             byte(result[0]),
                             byte(result[1]),
                             byte(result[2]),
                             byte(result[3]));
      first = FALSE;
    }
    if (self->filters.blur_radius > 0.0f) {
      g_string_append_printf(
          out, "%sblur=%g", first ? "" : ",", static_cast<double>(self->filters.blur_radius));
      first = FALSE;
    }
    if (self->filters.opacity < 1.0f) {
      g_string_append_printf(
          out, "%sopacity=%g", first ? "" : ",", static_cast<double>(self->filters.opacity));
      first = FALSE;
    }
    // Each drop shadow, with the standard deviation React Native parsed rather
    // than the radius GSK was given: that is the number both hosts were handed,
    // so it is the one a cross-host diff should compare. The AppKit side prints
    // the same.
    if (self->filter_shadows != nullptr) {
      for (guint i = 0; i < self->filter_shadows->len; i++) {
        const RnFilterShadow &shadow =
            g_array_index(self->filter_shadows, RnFilterShadow, i);
        g_string_append_printf(out,
                               "%sshadow=(%g,%g,%g,#%02x%02x%02x%02x)",
                               first ? "" : ",",
                               static_cast<double>(shadow.dx),
                               static_cast<double>(shadow.dy),
                               static_cast<double>(shadow.radius / 2.0f),
                               static_cast<unsigned>(shadow.color.red * 255.0 + 0.5),
                               static_cast<unsigned>(shadow.color.green * 255.0 + 0.5),
                               static_cast<unsigned>(shadow.color.blue * 255.0 + 0.5),
                               static_cast<unsigned>(shadow.color.alpha * 255.0 + 0.5));
        first = FALSE;
      }
    }
    g_string_append(out, ")");
  }

  // `hitSlop`, which is invisible in every other line of this dump: a view with
  // a bigger target is drawn exactly like one without.
  if (self->has_hit_slop) {
    g_string_append_printf(out,
                           " hit-slop=(%g,%g,%g,%g)",
                           static_cast<double>(self->hit_slop[0]),
                           static_cast<double>(self->hit_slop[1]),
                           static_cast<double>(self->hit_slop[2]),
                           static_cast<double>(self->hit_slop[3]));
  }
  // The resolved LABELLED_BY relation, by tag. The ids the app wrote are in its
  // own source; what is worth reporting is that they were resolved, and the tags
  // are Fabric's, so the two hosts print the same ones.
  if (self->labelled_by != nullptr && self->labelled_by->len > 0) {
    g_string_append(out, " labelled-by=");
    for (guint i = 0; i < self->labelled_by->len; i++) {
      g_string_append_printf(
          out, "%s%d", i == 0 ? "" : ",", g_array_index(self->labelled_by, int, i));
    }
  }
  // The resolved reading order, by tag and in the order the app asked for,
  // which is the whole point of the prop: the tags are Fabric's, so the two
  // hosts print the same line.
  if (self->accessibility_order != nullptr && self->accessibility_order->len > 0) {
    g_string_append(out, " a11y-order=");
    for (guint i = 0; i < self->accessibility_order->len; i++) {
      g_string_append_printf(
          out, "%s%d", i == 0 ? "" : ",", g_array_index(self->accessibility_order, int, i));
    }
  }
  if (self->texture != nullptr) {
    // The fit is here because it is the only thing about a drawn image that a
    // frame cannot show: two views the same size holding the same picture are
    // identical in every other line of this dump and different on screen.
    g_string_append_printf(out,
                           " texture=%dx%d fit=%s",
                           gdk_texture_get_width(self->texture),
                           gdk_texture_get_height(self->texture),
                           rn_image_fit_name(self->texture_fit));
    // That this image moves, which no other line can show: an animated GIF and
    // its first frame are the same size and the same picture in a snapshot.
    // Not which frame, deliberately -- the two hosts tick on their own clocks,
    // so a cross-host diff of that would be a race. Each suite asserts the
    // frames itself.
    if (self->animation != nullptr) {
      g_string_append(out, " animated=1");
    }
    // Printed for the same reason the fit is: a tinted image and an untinted
    // one are identical in every other line of this dump and different on
    // screen, and this is the only thing a test without pixels can read.
    if (self->has_image_tint) {
      g_string_append_printf(out,
                             " tint=#%02x%02x%02x%02x",
                             static_cast<unsigned>(self->image_tint.red * 255.0 + 0.5),
                             static_cast<unsigned>(self->image_tint.green * 255.0 + 0.5),
                             static_cast<unsigned>(self->image_tint.blue * 255.0 + 0.5),
                             static_cast<unsigned>(self->image_tint.alpha * 255.0 + 0.5));
    }
    // And the blur, which is invisible in this dump for the same reason and in
    // one more: the prop is read in a branch that knows ImageProps, and the
    // only thing that can say it got out of that branch and onto the widget is
    // a line here. It was once set inside the `tintColor` branch by accident,
    // where every test that pushed a blur node by hand still passed.
    if (self->image_blur > 0.0f) {
      g_string_append_printf(out, " blur=%g", static_cast<double>(self->image_blur));
    }
  }
  if (self->text_layout != nullptr) {
    const char *text = pango_layout_get_text(self->text_layout);
    if (text != nullptr && *text != '\0') {
      char *escaped = rn_escape_for_dump(text);
      g_string_append_printf(out, " text=\"%s\"", escaped);
      g_free(escaped);
    }
  }
  // The paragraph's writing direction, when an app asked for one. Nothing else
  // in this dump shows it: a right-to-left paragraph of Latin text has the same
  // box and the same string, and only the pixels differ. `natural` is printed
  // too, because asking for it is not the same as saying nothing -- a nested
  // <Text> inherits the enclosing direction otherwise.
  if (self->writing_direction != nullptr) {
    g_string_append_printf(out, " writing-dir=%s", self->writing_direction);
  }
  // What a field asked for about spelling. Neither word appears for a field
  // that said nothing, which is the third state rather than a default; what
  // each toolkit then did with it is asserted in its own suite, GTK having a
  // hint for the first and nothing at all for the second.
  if (self->spell_check != nullptr) {
    g_string_append_printf(out, " spellcheck=%s", self->spell_check);
  }
  if (self->auto_correct != nullptr) {
    g_string_append_printf(out, " autocorrect=%s", self->auto_correct);
  }
  // `autoCapitalize` and `keyboardType`, which this host turns into input hints
  // and an input purpose. Printed as the app wrote them so the two dumps
  // compare: AppKit can act on neither and says the same words.
  if (self->auto_capitalize != nullptr) {
    g_string_append_printf(out, " autocapitalize=%s", self->auto_capitalize);
  }
  // `caretHidden` and `contextMenuHidden`, printed only when asked for. Each is
  // the absence of something -- a blink, a menu -- so there is nothing else for
  // a test or the cross-host diff to look at.
  if (self->caret_hidden) {
    g_string_append(out, " caret=hidden");
  }
  if (self->context_menu_hidden) {
    g_string_append(out, " context-menu=hidden");
  }
  if (self->keyboard_type != nullptr) {
    g_string_append_printf(out, " keyboard=%s", self->keyboard_type);
  }
  // The paragraph's text shadow, which no other line of this dump can show: a
  // shadowed paragraph has the same text, the same colour and the same box. The
  // standard deviation React Native parsed rather than the radius GSK was
  // given, so the two hosts print the same number.
  if (self->text_shadow_color.alpha > 0.0f) {
    g_string_append_printf(out,
                           " text-shadow=(%g,%g,%g,#%02x%02x%02x%02x)",
                           static_cast<double>(self->text_shadow_dx),
                           static_cast<double>(self->text_shadow_dy),
                           static_cast<double>(self->text_shadow_radius / 2.0f),
                           static_cast<unsigned>(self->text_shadow_color.red * 255.0 + 0.5),
                           static_cast<unsigned>(self->text_shadow_color.green * 255.0 + 0.5),
                           static_cast<unsigned>(self->text_shadow_color.blue * 255.0 + 0.5),
                           static_cast<unsigned>(self->text_shadow_color.alpha * 255.0 + 0.5));
  }

  // A text field's content lives in its GtkText peer, not in a PangoLayout, so
  // it would otherwise be invisible to every test that reads this tree.
  if (self->editable != nullptr) {
    char *owned = rn_peer_get_text(self->editable);
    const char *value = owned;
    char *escaped = rn_escape_for_dump(value != nullptr ? value : "");
    g_string_append_printf(out, " editable=\"%s\"", escaped);
    g_free(owned);
    g_free(escaped);
    if (gtk_widget_has_focus(self->editable)) {
      g_string_append(out, " focused");
    }
  }

  // How many DevTools highlights this view is drawing. In the dump because they
  // are otherwise invisible to everything but a screenshot, and because a
  // command that arrived and drew nothing is exactly the failure worth
  // catching.
  if (self->highlight_filled != nullptr && self->highlight_filled->len > 0) {
    g_string_append_printf(out, " highlights=%u", self->highlight_filled->len);
  }

  // What kind of control this view is, and what state it is in. Written by
  // core/DesktopControls.h rather than formatted here, for the same reason the
  // role name below is React Native's vocabulary and not GTK's: three hosts
  // describing the same switch in three ways is a diff on every line.
  if (self->control_description != nullptr) {
    g_string_append_printf(out, " control=%s", self->control_description);
  }

  // React Native's role name, not GTK's. This dump is compared line by line
  // with the AppKit one, and each reporting its own toolkit's vocabulary would
  // make every accessible view look like a difference. That the *GTK* role was
  // really applied is asserted in tests/test_accessibility.cpp, which is where
  // a platform question belongs.
  if (self->role_name != nullptr && *self->role_name != '\0') {
    g_string_append_printf(out, " role=%s", self->role_name);
  }
  // `testID`, printed as the host was given it rather than as the platform
  // publishes it, which is the same division the role above uses: three hosts
  // reporting their own toolkit's answer would be a diff on every line, and
  // whether *this* toolkit really published it is a question for
  // tests/test_accessibility.cpp.
  if (self->test_id != nullptr && *self->test_id != '\0') {
    g_string_append_printf(out, " testid=%s", self->test_id);
  }
  // Whether Tab stops here. GTK's own answer rather than the flag this project
  // set, which is the stronger statement: it says the widget really did join
  // the focus chain. Whether it is focused *now* is deliberately not printed --
  // that depends on what the window manager did when the window opened, which
  // is not a property of the platform and would make this dump differ between
  // two machines running the same app.
  if (gtk_accessible_get_platform_state(GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PLATFORM_STATE_FOCUSABLE)) {
    g_string_append(out, " focusable");
  }
  // `accessibilityViewIsModal`, in React Native's words like the role and the
  // testID above. What each toolkit did with it is asserted in its own suite.
  if (self->accessible_modal) {
    g_string_append(out, " modal");
  }

  g_string_append_c(out, '\n');

  for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(self)); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (RN_IS_VIEW(child)) {
      rn_view_describe_into(RN_VIEW(child), out, depth + 1);
    }
  }
}

char *rn_view_describe_tree(RnView *self) {
  g_return_val_if_fail(RN_IS_VIEW(self), nullptr);
  GString *out = g_string_new(nullptr);
  rn_view_describe_into(self, out, 0);
  return g_string_free(out, FALSE);
}

void rn_view_insert_child(RnView *self, RnView *child, int index) {
  GtkWidget *child_widget = GTK_WIDGET(child);
  GtkWidget *parent_widget = GTK_WIDGET(self);

  // Fabric indexes into the parent's current child list. Walk to the sibling
  // currently at `index` and insert before it; a past-the-end index appends.
  GtkWidget *sibling = gtk_widget_get_first_child(parent_widget);
  for (int i = 0; i < index && sibling != nullptr; i++) {
    sibling = gtk_widget_get_next_sibling(sibling);
  }

  if (sibling != nullptr) {
    gtk_widget_insert_before(child_widget, parent_widget, sibling);
  } else {
    gtk_widget_set_parent(child_widget, parent_widget);
  }
}

void rn_view_remove_child(RnView * /*self*/, RnView *child) {
  // Remove detaches but must not destroy: a Delete mutation for the same tag
  // follows separately, and until then the view may be re-Inserted elsewhere.
  gtk_widget_unparent(GTK_WIDGET(child));
}
