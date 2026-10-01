#include "GtkTextPeer.h"

// A GtkTextView that draws a placeholder.
//
// GtkText has a placeholder property and GtkTextView has nothing, so the text
// is drawn -- over what the view drew, which is nothing when the buffer is
// empty, and only when it is empty.
//
// A subclass rather than an overlay because the peer is one widget parented
// inside the RnView, and the allocation path in RnView.cpp places exactly one.

#define RN_TYPE_TEXT_VIEW (rn_text_view_get_type())
G_DECLARE_FINAL_TYPE(RnTextView, rn_text_view, RN, TEXT_VIEW, GtkTextView)

struct _RnTextView {
  GtkTextView parent_instance;
  char *placeholder;
  PangoAttrList *attributes;
};

G_DEFINE_TYPE(RnTextView, rn_text_view, GTK_TYPE_TEXT_VIEW)

static void rn_text_view_snapshot(GtkWidget *widget, GtkSnapshot *snapshot) {
  GTK_WIDGET_CLASS(rn_text_view_parent_class)->snapshot(widget, snapshot);

  RnTextView *self = RN_TEXT_VIEW(widget);
  if (self->placeholder == nullptr || *self->placeholder == '\0') {
    return;
  }
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(widget));
  if (gtk_text_buffer_get_char_count(buffer) > 0) {
    return;
  }

  PangoLayout *layout = gtk_widget_create_pango_layout(widget, self->placeholder);
  if (self->attributes != nullptr) {
    pango_layout_set_attributes(layout, self->attributes);
  }

  // The text colour, dimmed. GTK's own placeholder is the theme's dim
  // foreground, and this field's foreground came from React Native's props --
  // so dimming that keeps the placeholder legible against whatever background
  // the app chose, which a fixed grey would not.
  GdkRGBA colour = {0.5F, 0.5F, 0.5F, 0.5F};
  if (self->attributes != nullptr) {
    PangoAttrIterator *iter = pango_attr_list_get_iterator(self->attributes);
    if (iter != nullptr) {
      if (const PangoAttribute *found = pango_attr_iterator_get(iter, PANGO_ATTR_FOREGROUND)) {
        const PangoColor &from = reinterpret_cast<const PangoAttrColor *>(found)->color;
        colour.red = static_cast<float>(from.red / 65535.0);
        colour.green = static_cast<float>(from.green / 65535.0);
        colour.blue = static_cast<float>(from.blue / 65535.0);
      }
      pango_attr_iterator_destroy(iter);
    }
  }
  colour.alpha = 0.45F;

  gtk_snapshot_append_layout(snapshot, layout, &colour);
  g_object_unref(layout);
}

static void rn_text_view_finalize(GObject *object) {
  RnTextView *self = RN_TEXT_VIEW(object);
  g_clear_pointer(&self->placeholder, g_free);
  g_clear_pointer(&self->attributes, pango_attr_list_unref);
  G_OBJECT_CLASS(rn_text_view_parent_class)->finalize(object);
}

static void rn_text_view_class_init(RnTextViewClass *klass) {
  G_OBJECT_CLASS(klass)->finalize = rn_text_view_finalize;
  GTK_WIDGET_CLASS(klass)->snapshot = rn_text_view_snapshot;
}

static void rn_text_view_init(RnTextView *self) {
  self->placeholder = nullptr;
  self->attributes = nullptr;
}

// Empty to non-empty and back changes whether the placeholder belongs on
// screen, and GTK has no reason to know that.
static void on_buffer_changed_redraw(GtkTextBuffer * /*buffer*/, gpointer widget) {
  gtk_widget_queue_draw(GTK_WIDGET(widget));
}

// A GtkTextView paints a background of its own, from the GTK theme, straight
// over the one the RnView drew from React Native's props -- which is a white
// box where a styled field should be, and white text invisible inside it. A
// bare GtkText has no such background, which is why the single-line path never
// needed this.
//
// Installed once, on the display, and scoped to the class RnView already puts
// on every peer.
static void ensure_peer_css(void) {
  static gboolean installed = FALSE;
  if (installed) {
    return;
  }
  GdkDisplay *display = gdk_display_get_default();
  if (display == nullptr) {
    // No display yet; a peer built before one exists is not going to be drawn
    // either, and the next one will install this.
    return;
  }
  installed = TRUE;

  GtkCssProvider *provider = gtk_css_provider_new();
  // Both nodes: a GtkTextView draws through a child "text" node, and leaving
  // either opaque leaves the field opaque.
  gtk_css_provider_load_from_string(
      provider,
      ".rn-text-input, .rn-text-input text { background: none; background-color: transparent; }");
  gtk_style_context_add_provider_for_display(
      display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  g_object_unref(provider);
}

GtkWidget *rn_peer_new(gboolean multiline) {
  if (!multiline) {
    return gtk_text_new();
  }

  ensure_peer_css();
  GtkWidget *view = GTK_WIDGET(g_object_new(RN_TYPE_TEXT_VIEW, nullptr));
  g_signal_connect(gtk_text_view_get_buffer(GTK_TEXT_VIEW(view)), "changed",
                   G_CALLBACK(on_buffer_changed_redraw), view);
  // The measured box is already tall enough for the wrapped text -- the shadow
  // node measures a multiline input against the real constraints -- so without
  // wrapping here the text would run off one line inside a box sized for
  // three. WORD_CHAR rather than WORD so a single long word still breaks,
  // which is what every other platform's multiline field does.
  gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(view), GTK_WRAP_WORD_CHAR);
  // No margins of its own: the RnView already allocates the peer inside the
  // content inset Yoga resolved.
  gtk_text_view_set_top_margin(GTK_TEXT_VIEW(view), 0);
  gtk_text_view_set_bottom_margin(GTK_TEXT_VIEW(view), 0);
  gtk_text_view_set_left_margin(GTK_TEXT_VIEW(view), 0);
  gtk_text_view_set_right_margin(GTK_TEXT_VIEW(view), 0);
  return view;
}

gboolean rn_peer_is_multiline(GtkWidget *peer) {
  return peer != nullptr && GTK_IS_TEXT_VIEW(peer);
}

static GtkTextBuffer *buffer_of(GtkWidget *peer) {
  return gtk_text_view_get_buffer(GTK_TEXT_VIEW(peer));
}

char *rn_peer_get_text(GtkWidget *peer) {
  if (peer == nullptr) {
    return g_strdup("");
  }
  if (!rn_peer_is_multiline(peer)) {
    const char *text = gtk_editable_get_text(GTK_EDITABLE(peer));
    return g_strdup(text != nullptr ? text : "");
  }

  GtkTextBuffer *buffer = buffer_of(peer);
  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  // FALSE: invisible characters and widget anchors are not the user's text.
  return gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
}

void rn_peer_set_text(GtkWidget *peer, const char *text) {
  if (peer == nullptr) {
    return;
  }
  const char *value = text != nullptr ? text : "";
  if (!rn_peer_is_multiline(peer)) {
    gtk_editable_set_text(GTK_EDITABLE(peer), value);
    return;
  }
  gtk_text_buffer_set_text(buffer_of(peer), value, -1);
}

int rn_peer_get_position(GtkWidget *peer) {
  if (peer == nullptr) {
    return 0;
  }
  if (!rn_peer_is_multiline(peer)) {
    return gtk_editable_get_position(GTK_EDITABLE(peer));
  }

  GtkTextBuffer *buffer = buffer_of(peer);
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_mark(buffer, &iter, gtk_text_buffer_get_insert(buffer));
  return gtk_text_iter_get_offset(&iter);
}

void rn_peer_set_position(GtkWidget *peer, int position) {
  if (peer == nullptr) {
    return;
  }
  if (!rn_peer_is_multiline(peer)) {
    gtk_editable_set_position(GTK_EDITABLE(peer), position);
    return;
  }

  GtkTextBuffer *buffer = buffer_of(peer);
  GtkTextIter iter;
  // Clamped, because an offset past the end is a runtime warning and then a
  // caret nowhere in particular.
  const int length = gtk_text_buffer_get_char_count(buffer);
  gtk_text_buffer_get_iter_at_offset(buffer, &iter, CLAMP(position, 0, length));
  gtk_text_buffer_place_cursor(buffer, &iter);
}

gboolean rn_peer_get_selection_bounds(GtkWidget *peer, int *start, int *end) {
  if (peer == nullptr) {
    return FALSE;
  }
  if (!rn_peer_is_multiline(peer)) {
    return gtk_editable_get_selection_bounds(GTK_EDITABLE(peer), start, end);
  }

  GtkTextIter from;
  GtkTextIter to;
  if (!gtk_text_buffer_get_selection_bounds(buffer_of(peer), &from, &to)) {
    return FALSE;
  }
  if (start != nullptr) {
    *start = gtk_text_iter_get_offset(&from);
  }
  if (end != nullptr) {
    *end = gtk_text_iter_get_offset(&to);
  }
  return TRUE;
}

void rn_peer_select_region(GtkWidget *peer, int start, int end) {
  if (peer == nullptr) {
    return;
  }
  if (!rn_peer_is_multiline(peer)) {
    gtk_editable_select_region(GTK_EDITABLE(peer), start, end);
    return;
  }

  GtkTextBuffer *buffer = buffer_of(peer);
  const int length = gtk_text_buffer_get_char_count(buffer);
  GtkTextIter from;
  GtkTextIter to;
  gtk_text_buffer_get_iter_at_offset(buffer, &from, CLAMP(start, 0, length));
  gtk_text_buffer_get_iter_at_offset(buffer, &to, CLAMP(end, 0, length));
  gtk_text_buffer_select_range(buffer, &from, &to);
}

void rn_peer_set_editable(GtkWidget *peer, gboolean editable) {
  if (peer == nullptr) {
    return;
  }
  if (!rn_peer_is_multiline(peer)) {
    gtk_editable_set_editable(GTK_EDITABLE(peer), editable);
    return;
  }
  gtk_text_view_set_editable(GTK_TEXT_VIEW(peer), editable);
  // Without this a read-only view still shows a blinking caret, which reads as
  // a field that has focus and refuses to type.
  gtk_text_view_set_cursor_visible(GTK_TEXT_VIEW(peer), editable);
}

GObject *rn_peer_signal_source(GtkWidget *peer) {
  if (peer == nullptr) {
    return nullptr;
  }
  // A GtkText emits "changed" and carries the cursor properties itself; a
  // GtkTextView emits neither -- its buffer does. Handing back the right
  // object is what lets the manager connect one set of handlers.
  return rn_peer_is_multiline(peer) ? G_OBJECT(buffer_of(peer)) : G_OBJECT(peer);
}

void rn_peer_set_attributes(GtkWidget *peer, PangoAttrList *attributes) {
  if (peer == nullptr) {
    return;
  }
  if (!rn_peer_is_multiline(peer)) {
    gtk_text_set_attributes(GTK_TEXT(peer), attributes);
    return;
  }

  // A tag over the whole buffer. Named, so the same one is reused and the tag
  // table does not grow a new entry on every prop update -- which it would,
  // and a controlled field updates on every keystroke.
  // Kept for the placeholder, which is drawn rather than laid out by the
  // buffer and so needs the font and colour in its own right.
  RnTextView *self = RN_TEXT_VIEW(peer);
  g_clear_pointer(&self->attributes, pango_attr_list_unref);
  self->attributes = attributes != nullptr ? pango_attr_list_ref(attributes) : nullptr;

  GtkTextBuffer *buffer = buffer_of(peer);
  GtkTextTagTable *table = gtk_text_buffer_get_tag_table(buffer);
  GtkTextTag *tag = gtk_text_tag_table_lookup(table, "rn-style");
  if (tag == nullptr) {
    tag = gtk_text_buffer_create_tag(buffer, "rn-style", nullptr);
  }
  // A GtkTextTag has no PangoAttrList property -- it carries the same
  // information as individual properties -- so the two that matter are pulled
  // back out of the list. Font and colour are what the single-line path sets
  // and what a field left in the GTK theme gets wrong: dark text on a dark
  // background.
  if (attributes != nullptr) {
    PangoAttrIterator *iter = pango_attr_list_get_iterator(attributes);
    if (iter != nullptr) {
      PangoFontDescription *font = pango_font_description_new();
      pango_attr_iterator_get_font(iter, font, nullptr, nullptr);
      g_object_set(tag, "font-desc", font, nullptr);
      pango_font_description_free(font);

      if (const PangoAttribute *found = pango_attr_iterator_get(iter, PANGO_ATTR_FOREGROUND)) {
        const PangoColor &colour = reinterpret_cast<const PangoAttrColor *>(found)->color;
        GdkRGBA rgba;
        rgba.red = colour.red / 65535.0;
        rgba.green = colour.green / 65535.0;
        rgba.blue = colour.blue / 65535.0;
        rgba.alpha = 1.0;
        g_object_set(tag, "foreground-rgba", &rgba, nullptr);
      }
      pango_attr_iterator_destroy(iter);
    }
  }

  GtkTextIter start;
  GtkTextIter end;
  gtk_text_buffer_get_bounds(buffer, &start, &end);
  gtk_text_buffer_apply_tag(buffer, tag, &start, &end);
}

void rn_peer_set_placeholder(GtkWidget *peer, const char *placeholder) {
  if (peer == nullptr) {
    return;
  }
  const char *value = placeholder != nullptr && *placeholder != '\0' ? placeholder : nullptr;
  if (!rn_peer_is_multiline(peer)) {
    gtk_text_set_placeholder_text(GTK_TEXT(peer), value);
    return;
  }

  RnTextView *self = RN_TEXT_VIEW(peer);
  g_clear_pointer(&self->placeholder, g_free);
  self->placeholder = value != nullptr ? g_strdup(value) : nullptr;
  gtk_widget_queue_draw(peer);
}

const char *rn_peer_get_placeholder(GtkWidget *peer) {
  if (peer == nullptr) {
    return nullptr;
  }
  if (!rn_peer_is_multiline(peer)) {
    return gtk_text_get_placeholder_text(GTK_TEXT(peer));
  }
  return RN_TEXT_VIEW(peer)->placeholder;
}

void rn_peer_set_visibility(GtkWidget *peer, gboolean visible) {
  if (peer == nullptr || rn_peer_is_multiline(peer)) {
    return;
  }
  gtk_text_set_visibility(GTK_TEXT(peer), visible);
}

// The buffer's half of maxLength. GtkText has a property for it; GtkTextBuffer
// has nothing, so the limit is enforced by refusing the insertion that would
// cross it -- which is what GtkText does internally too.
static void on_buffer_insert(GtkTextBuffer *buffer,
                             GtkTextIter * /*location*/,
                             const char *text,
                             int length,
                             gpointer /*data*/) {
  const int limit = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(buffer), "rn-max-length"));
  if (limit <= 0) {
    return;
  }
  const int existing = gtk_text_buffer_get_char_count(buffer);
  const long incoming = g_utf8_strlen(text, length);
  if (existing + incoming <= limit) {
    return;
  }
  // Refused whole rather than truncated: a paste that would overflow is
  // rejected, which is what GtkText does with its own max-length and what
  // stops a half-pasted string appearing.
  g_signal_stop_emission_by_name(buffer, "insert-text");
}

void rn_peer_set_max_length(GtkWidget *peer, int max_length) {
  if (peer == nullptr) {
    return;
  }
  if (!rn_peer_is_multiline(peer)) {
    gtk_text_set_max_length(GTK_TEXT(peer), max_length);
    return;
  }

  GtkTextBuffer *buffer = buffer_of(peer);
  g_object_set_data(G_OBJECT(buffer), "rn-max-length", GINT_TO_POINTER(max_length));
  // Connected once. The handler reads the limit back out of the buffer, so a
  // changed maxLength needs no reconnection.
  if (g_object_get_data(G_OBJECT(buffer), "rn-max-length-connected") == nullptr) {
    g_object_set_data(G_OBJECT(buffer), "rn-max-length-connected", GINT_TO_POINTER(1));
    g_signal_connect(buffer, "insert-text", G_CALLBACK(on_buffer_insert), nullptr);
  }
}

void rn_peer_insert_text(GtkWidget *peer, const char *text, int length, int *position) {
  if (peer == nullptr || text == nullptr) {
    return;
  }
  if (!rn_peer_is_multiline(peer)) {
    gtk_editable_insert_text(GTK_EDITABLE(peer), text, length, position);
    return;
  }

  GtkTextBuffer *buffer = buffer_of(peer);
  const int count = gtk_text_buffer_get_char_count(buffer);
  const int at = position != nullptr ? CLAMP(*position, 0, count) : count;
  GtkTextIter iter;
  gtk_text_buffer_get_iter_at_offset(buffer, &iter, at);
  gtk_text_buffer_insert(buffer, &iter, text, length);
  if (position != nullptr) {
    // Where the caret ended up: insert leaves `iter` past what it wrote.
    *position = gtk_text_iter_get_offset(&iter);
  }
}
