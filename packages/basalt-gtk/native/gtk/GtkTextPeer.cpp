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

  // `placeholderTextColor`. The single-line peer takes it from CSS, because
  // GtkText's placeholder is a CSS node GTK draws; this one has no such node
  // precisely because the placeholder is drawn below instead, so the colour has
  // to arrive here as a value. Unset leaves the drawing to work the colour out
  // from the text colour, which is what it did before the prop was honoured at
  // all.
  GdkRGBA placeholder_color;
  gboolean has_placeholder_color;
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

  // `placeholderTextColor` when the app named one, used exactly as given: a
  // colour somebody chose is not dimmed, and its own alpha is the alpha they
  // asked for rather than one this file picks.
  //
  // Otherwise the text colour, dimmed. GTK's own placeholder is the theme's dim
  // foreground, and this field's foreground came from React Native's props --
  // so dimming that keeps the placeholder legible against whatever background
  // the app chose, which a fixed grey would not.
  GdkRGBA colour = {0.5F, 0.5F, 0.5F, 0.5F};
  if (self->has_placeholder_color) {
    colour = self->placeholder_color;
  } else {
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
  }

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
  self->has_placeholder_color = FALSE;
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

void rn_peer_set_spell_check(GtkWidget *peer, int flag) {
  if (peer == nullptr || flag == 0) {
    // Unset leaves whatever the input method was configured to do, which is a
    // real answer and not a missing one.
    return;
  }

  // Read, changed, written back: the hints are a bitmask and this owns only one
  // pair of bits. A plain set would drop whatever else put a hint there --
  // nothing does today, and the next prop in this cluster will.
  const GtkInputHints wanted =
      flag == 1 ? GTK_INPUT_HINT_SPELLCHECK : GTK_INPUT_HINT_NO_SPELLCHECK;
  const GtkInputHints unwanted =
      flag == 1 ? GTK_INPUT_HINT_NO_SPELLCHECK : GTK_INPUT_HINT_SPELLCHECK;

  if (!rn_peer_is_multiline(peer)) {
    const GtkInputHints hints = gtk_text_get_input_hints(GTK_TEXT(peer));
    gtk_text_set_input_hints(
        GTK_TEXT(peer), static_cast<GtkInputHints>((hints & ~unwanted) | wanted));
    return;
  }
  const GtkInputHints hints = gtk_text_view_get_input_hints(GTK_TEXT_VIEW(peer));
  gtk_text_view_set_input_hints(
      GTK_TEXT_VIEW(peer), static_cast<GtkInputHints>((hints & ~unwanted) | wanted));
}

void rn_peer_set_auto_capitalize(GtkWidget *peer, int type) {
  if (peer == nullptr) {
    return;
  }
  // The three are exclusive and this owns all of them, so the mask is cleared
  // before one is set -- and `none` sets none at all, which is the honest way
  // to say "do not capitalise for me": GTK_INPUT_HINT_LOWERCASE would ask the
  // input method to lowercase what the person typed, which no app asked for.
  const GtkInputHints owned = static_cast<GtkInputHints>(
      GTK_INPUT_HINT_UPPERCASE_CHARS | GTK_INPUT_HINT_UPPERCASE_WORDS
      | GTK_INPUT_HINT_UPPERCASE_SENTENCES);
  GtkInputHints wanted = GTK_INPUT_HINT_NONE;
  switch (type) {
    case 1:
      wanted = GTK_INPUT_HINT_UPPERCASE_WORDS;
      break;
    case 2:
      wanted = GTK_INPUT_HINT_UPPERCASE_SENTENCES;
      break;
    case 3:
      wanted = GTK_INPUT_HINT_UPPERCASE_CHARS;
      break;
    default:
      break;
  }

  if (!rn_peer_is_multiline(peer)) {
    const GtkInputHints hints = gtk_text_get_input_hints(GTK_TEXT(peer));
    gtk_text_set_input_hints(GTK_TEXT(peer),
                             static_cast<GtkInputHints>((hints & ~owned) | wanted));
    return;
  }
  const GtkInputHints hints = gtk_text_view_get_input_hints(GTK_TEXT_VIEW(peer));
  gtk_text_view_set_input_hints(GTK_TEXT_VIEW(peer),
                                static_cast<GtkInputHints>((hints & ~owned) | wanted));
}

void rn_peer_set_input_purpose(GtkWidget *peer, int purpose) {
  if (peer == nullptr) {
    return;
  }
  if (!rn_peer_is_multiline(peer)) {
    gtk_text_set_input_purpose(GTK_TEXT(peer), static_cast<GtkInputPurpose>(purpose));
    return;
  }
  gtk_text_view_set_input_purpose(GTK_TEXT_VIEW(peer),
                                  static_cast<GtkInputPurpose>(purpose));
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

// ---------------------------------------------------------------------------
// contextMenuHidden
// ---------------------------------------------------------------------------

namespace {

// The gesture is remembered on the widget so the prop can be turned back on,
// and so that a controlled field re-sending identical props does not install a
// second one on every keystroke.
constexpr const char *kContextMenuGesture = "rn-context-menu-gesture";

// Claiming the sequence in the capture phase is the whole trick: a claimed
// sequence is cancelled for every other gesture on the way down, including the
// one GtkText uses to open its menu. Returning without claiming would let the
// menu open as usual.
void swallow_secondary_click(GtkGestureClick *gesture,
                             int /*n_press*/,
                             double /*x*/,
                             double /*y*/,
                             gpointer /*data*/) {
  gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
}

} // namespace

void rn_peer_set_context_menu_hidden(GtkWidget *peer, gboolean hidden) {
  if (peer == nullptr) {
    return;
  }
  GtkEventController *installed =
      GTK_EVENT_CONTROLLER(g_object_get_data(G_OBJECT(peer), kContextMenuGesture));
  if (!hidden) {
    if (installed != nullptr) {
      gtk_widget_remove_controller(peer, installed);
      g_object_set_data(G_OBJECT(peer), kContextMenuGesture, nullptr);
    }
    return;
  }
  if (installed != nullptr) {
    return;
  }

  GtkGesture *gesture = gtk_gesture_click_new();
  // The secondary button only: the primary one places the caret and must go on
  // doing that.
  gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(gesture), GDK_BUTTON_SECONDARY);
  gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(gesture), GTK_PHASE_CAPTURE);
  g_signal_connect(gesture, "pressed", G_CALLBACK(swallow_secondary_click), nullptr);
  gtk_widget_add_controller(peer, GTK_EVENT_CONTROLLER(gesture));
  // Unowned: the widget owns the controller now, and this is only a note that
  // one is there.
  g_object_set_data(G_OBJECT(peer), kContextMenuGesture, gesture);
}

gboolean rn_peer_context_menu_hidden(GtkWidget *peer) {
  if (peer == nullptr) {
    return FALSE;
  }
  return g_object_get_data(G_OBJECT(peer), kContextMenuGesture) != nullptr ? TRUE : FALSE;
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

// ---------------------------------------------------------------------------
// placeholderTextColor, selectionColor and cursorColor
//
// These three are the only props a <TextInput> has that GTK will not take as a
// value. The placeholder and the selection are CSS nodes under the peer, and
// the caret is the CSS `caret-color` property; none of the three is a widget
// property and none of them is expressible in the PangoAttrList that carries
// `color`, `fontSize` and the rest. So they go in as a stylesheet.
// ---------------------------------------------------------------------------

// The eight hex digits that identify a colour inside a CSS class name, or
// "none" for a colour that was never asked for. Unambiguous without a
// separator, "none" containing two letters that are not hex digits, but
// separated anyway so a name is readable in a GTK inspector.
static void append_color_tag(GString *out, const GdkRGBA *colour) {
  g_string_append_c(out, '-');
  if (colour == nullptr) {
    g_string_append(out, "none");
    return;
  }
  const float channels[4] = {colour->red, colour->green, colour->blue, colour->alpha};
  for (int i = 0; i < 4; i++) {
    g_string_append_printf(
        out, "%02x", static_cast<unsigned>(CLAMP(channels[i], 0.0F, 1.0F) * 255.0F + 0.5F));
  }
}

// The rules for one combination of colours, hung on `name`.
//
// The selectors are descendant rather than child selectors because the class
// sits on the peer and the two peers nest differently: a GtkText's `placeholder`
// and `selection` are its own children, a GtkTextView's `selection` is a child
// of its `text` node. A descendant selector is true of both.
//
// Colours are formatted by GDK rather than by hand, which is what gets the
// alpha right: gdk_rgba_to_string emits `rgb(r,g,b)` for an opaque colour and
// `rgba(r,g,b,a)` otherwise, in the one spelling gdk_rgba_parse accepts back,
// and it formats the alpha through g_ascii_formatd so a French locale does not
// write a decimal comma into a stylesheet.
static void append_color_rules(GString *css,
                               const char *name,
                               const GdkRGBA *placeholder,
                               const GdkRGBA *selection,
                               const GdkRGBA *cursor) {
  if (placeholder != nullptr) {
    char *value = gdk_rgba_to_string(placeholder);
    g_string_append_printf(css, ".%s placeholder { color: %s; }\n", name, value);
    g_free(value);
  }
  if (selection != nullptr) {
    // The background, not the colour: GTK draws a selection as a filled node
    // behind the text, and `color` there is the *selected text*, which React
    // Native has no prop for and which must keep coming from the style.
    char *value = gdk_rgba_to_string(selection);
    g_string_append_printf(css, ".%s selection { background-color: %s; }\n", name, value);
    g_free(value);
  }
  if (cursor != nullptr) {
    // Both carets. A bidirectional line has two, and leaving the secondary one
    // in the theme's colour next to a caret the app chose would look like a
    // bug rather than like the feature it is. The `text` node is named as well
    // as the peer itself because the multiline peer's caret is drawn by that
    // child node.
    char *value = gdk_rgba_to_string(cursor);
    g_string_append_printf(
        css,
        ".%s, .%s text { caret-color: %s; -gtk-secondary-caret-color: %s; }\n",
        name,
        name,
        value,
        value);
    g_free(value);
  }
}

// The CSS class carrying these colours, with the rules for it installed on the
// display the first time the combination is seen.
//
// Display-wide rather than a provider per field, which is the obvious reading
// of "per-widget CSS": the only per-widget route GTK offers is
// gtk_widget_get_style_context, deprecated since 4.10 and already behind
// gtk/deprecated/ in 4.22. GtkMountingManager.cpp reached the same conclusion
// for a <Switch>'s track colour and says so there, so this does it the same way
// rather than introducing a second way that also has to be rewritten for GTK 5.
//
// The name is derived from the colours rather than handed out in order, which
// is what makes that affordable. Two fields given the same colours share one
// rule, so an app with one theme installs one rule however many fields it has;
// and a field whose props are re-sent unchanged -- which a controlled field's
// are, on every single keystroke -- resolves to the class it already carries
// and installs nothing at all.
static const char *color_class_for(const GdkRGBA *placeholder,
                                   const GdkRGBA *selection,
                                   const GdkRGBA *cursor) {
  GString *name = g_string_new("rn-input-colors");
  append_color_tag(name, placeholder);
  append_color_tag(name, selection);
  append_color_tag(name, cursor);
  const char *interned = g_intern_string(name->str);
  g_string_free(name, TRUE);

  // Interned, so the set can be keyed on the pointer. Never emptied: a rule
  // dropped while some widget still carried its class would silently lose that
  // widget's colour, and the set is bounded by the number of distinct colour
  // combinations an app uses rather than by how long it runs.
  static GHashTable *installed = nullptr;
  static GString *css = nullptr;
  static GtkCssProvider *provider = nullptr;
  if (installed == nullptr) {
    installed = g_hash_table_new(g_direct_hash, g_direct_equal);
    css = g_string_new(nullptr);
  }
  if (g_hash_table_contains(installed, interned)) {
    return interned;
  }
  g_hash_table_add(installed, const_cast<char *>(interned));
  append_color_rules(css, interned, placeholder, selection, cursor);

  GdkDisplay *display = gdk_display_get_default();
  if (display == nullptr) {
    // No display to hang a provider on, and nothing is being drawn either. The
    // rules are kept rather than skipped, so they reach GTK with whatever
    // combination arrives next once there is a display: the provider is loaded
    // with the whole accumulated stylesheet every time, not with one rule.
    return interned;
  }
  if (provider == nullptr) {
    provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }
  gtk_css_provider_load_from_string(provider, css->str);
  return interned;
}

void rn_peer_set_colors(GtkWidget *peer,
                        const GdkRGBA *placeholder,
                        const GdkRGBA *selection,
                        const GdkRGBA *cursor) {
  if (peer == nullptr) {
    return;
  }

  // The multiline peer's placeholder is drawn by this file, so it has no CSS
  // node for the rule above to reach and takes the colour as a value instead.
  // The other two are CSS on either peer.
  if (rn_peer_is_multiline(peer)) {
    RnTextView *self = RN_TEXT_VIEW(peer);
    self->has_placeholder_color = placeholder != nullptr ? TRUE : FALSE;
    if (placeholder != nullptr) {
      self->placeholder_color = *placeholder;
    }
    gtk_widget_queue_draw(peer);
  }

  const char *name = nullptr;
  if (placeholder != nullptr || selection != nullptr || cursor != nullptr) {
    name = color_class_for(placeholder, selection, cursor);
  }

  const char *previous =
      static_cast<const char *>(g_object_get_data(G_OBJECT(peer), "rn-colors-class"));
  if (previous == name) {
    // The common case by far, and the reason the class name is derived from the
    // colours: a controlled field re-applies every one of its props on every
    // keystroke, and re-adding a class it already has would be churn for
    // nothing. Pointer equality is a real comparison here because both sides
    // are interned, and it covers "neither is set" too.
    return;
  }
  if (previous != nullptr) {
    // Taken off, not merely overridden. A field whose colour changed from red
    // to blue would otherwise carry both classes, both rules would match, and
    // which of the two won would be whichever the provider happened to parse
    // last rather than the one React asked for.
    gtk_widget_remove_css_class(peer, previous);
  }
  // Interned and therefore immortal, so this holds a borrowed pointer and needs
  // no destroy notify. NULL removes the association, which is what a field that
  // has just lost all three props wants.
  g_object_set_data(G_OBJECT(peer), "rn-colors-class", const_cast<char *>(name));

  char *rules = nullptr;
  if (name != nullptr) {
    gtk_widget_add_css_class(peer, name);
    GString *built = g_string_new(nullptr);
    append_color_rules(built, name, placeholder, selection, cursor);
    rules = g_string_free(built, FALSE);
  }
  g_object_set_data_full(G_OBJECT(peer), "rn-colors-css", rules, g_free);
}

const char *rn_peer_get_colors_class(GtkWidget *peer) {
  if (peer == nullptr) {
    return nullptr;
  }
  return static_cast<const char *>(g_object_get_data(G_OBJECT(peer), "rn-colors-class"));
}

const char *rn_peer_get_colors_css(GtkWidget *peer) {
  if (peer == nullptr) {
    return nullptr;
  }
  return static_cast<const char *>(g_object_get_data(G_OBJECT(peer), "rn-colors-css"));
}

gboolean rn_peer_get_placeholder_color(GtkWidget *peer, GdkRGBA *out) {
  if (peer == nullptr || !rn_peer_is_multiline(peer)) {
    return FALSE;
  }
  RnTextView *self = RN_TEXT_VIEW(peer);
  if (!self->has_placeholder_color) {
    return FALSE;
  }
  if (out != nullptr) {
    *out = self->placeholder_color;
  }
  return TRUE;
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
