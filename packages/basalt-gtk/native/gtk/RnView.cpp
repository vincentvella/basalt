#include "RnView.h"

#include "Backface.h"
#include "ScrollIndicator.h"

#include "ControlMetrics.h"
#include "FocusRing.h"
#include "GtkTextPeer.h"

#include <cstring>

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
  double indicator_v_offset;
  double indicator_v_length;
  double indicator_h_offset;
  double indicator_h_length;
  gboolean has_image_tint;
  GdkRGBA image_tint;
  char *role_name;
  // The app's `nativeID`. Read only by the hidden title bar's drag-region hit
  // test; see RnView.h and core/TitleBarRegions.h.
  char *native_id;

  gboolean clips_children;
  double scroll_x;
  double scroll_y;

  graphene_size_t border_radii[4];
  gboolean has_border_radii;
  float border_widths[4];
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

G_DEFINE_TYPE(RnView, rn_view, GTK_TYPE_WIDGET)

static void rn_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  RnView *self = RN_VIEW(widget);

  const int width = gtk_widget_get_width(widget);
  const int height = gtk_widget_get_height(widget);

  const gboolean needs_opacity_layer = self->opacity < 1.0;
  if (needs_opacity_layer) {
    gtk_snapshot_push_opacity(snapshot, self->opacity);
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

  // Content clipping is separate, and only happens with overflow: 'hidden'.
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
    gtk_snapshot_append_layout(snapshot, self->text_layout, &self->text_color);
  }

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
      gtk_widget_snapshot_child(widget, child, snapshot);
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
      gtk_widget_snapshot_child(widget, GTK_WIDGET(g_ptr_array_index(ordered, i)), snapshot);
    }
    g_ptr_array_free(ordered, TRUE);
  }

  if (self->clips_children) {
    gtk_snapshot_pop(snapshot);
  }

  // The scrollbars: an overlay, so above the content and above any children,
  // which is what "overlay indicator" means -- and below the border, the
  // DevTools overlay and the focus ring, none of which belongs to the app.
  if (self->indicator_v_length > 0.0 || self->indicator_h_length > 0.0) {
    const float thickness = static_cast<float>(basalt::kScrollIndicatorThickness);
    const float inset = static_cast<float>(basalt::kScrollIndicatorInset);
    // Neutral and translucent, so it reads over light and dark content alike.
    const GdkRGBA thumb{0.0F, 0.0F, 0.0F, 0.35F};

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
    gtk_snapshot_append_border(snapshot, &box, self->border_widths, self->border_colors);
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
  g_clear_pointer(&self->role_name, g_free);
  g_clear_pointer(&self->native_id, g_free);

  G_OBJECT_CLASS(rn_view_parent_class)->dispose(object);
}

static void rn_view_class_init(RnViewClass *klass) {
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);

  object_class->dispose = rn_view_dispose;
  widget_class->snapshot = rn_view_snapshot;

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
  self->clips_children = FALSE;
  self->scroll_x = 0.0;
  self->scroll_y = 0.0;
  for (int i = 0; i < 4; i++) {
    self->border_radii[i] = graphene_size_t{0.0f, 0.0f};
    self->border_widths[i] = 0.0f;
    self->border_colors[i] = GdkRGBA{0.0f, 0.0f, 0.0f, 0.0f};
  }
  self->has_border_radii = FALSE;
  self->has_borders = FALSE;
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

void rn_view_set_role_name(RnView *self, const char *name) {
  g_return_if_fail(RN_IS_VIEW(self));
  g_free(self->role_name);
  self->role_name = name != nullptr && *name != '\0' ? g_strdup(name) : nullptr;
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
  // Printed only when it is not the default, like every other field here.
  // Worth printing at all because it is invisible: a view with
  // `pointerEvents: none` is drawn exactly like one without, and the only way
  // the cross-host diff can say the prop arrived on all three is if each one
  // reports it.
  if (self->pointer_events != RN_POINTER_EVENTS_AUTO) {
    g_string_append_printf(out, " pe=%s", rn_pointer_events_name(self->pointer_events));
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
  }
  if (self->text_layout != nullptr) {
    const char *text = pango_layout_get_text(self->text_layout);
    if (text != nullptr && *text != '\0') {
      char *escaped = rn_escape_for_dump(text);
      g_string_append_printf(out, " text=\"%s\"", escaped);
      g_free(escaped);
    }
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
  // Whether Tab stops here. GTK's own answer rather than the flag this project
  // set, which is the stronger statement: it says the widget really did join
  // the focus chain. Whether it is focused *now* is deliberately not printed --
  // that depends on what the window manager did when the window opened, which
  // is not a property of the platform and would make this dump differ between
  // two machines running the same app.
  if (gtk_accessible_get_platform_state(GTK_ACCESSIBLE(self), GTK_ACCESSIBLE_PLATFORM_STATE_FOCUSABLE)) {
    g_string_append(out, " focusable");
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
