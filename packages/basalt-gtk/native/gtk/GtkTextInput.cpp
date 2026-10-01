#include "GtkTextInput.h"

#include "GtkTextPeer.h"

#include "PangoTextLayout.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>

#include <cmath>
#include <cstdint>
#include <string_view>

namespace basalt {

using facebook::react::AttributedString;
using facebook::react::ShadowView;
using facebook::react::Tag;
using facebook::react::TextInputEventEmitter;
using facebook::react::TextInputProps;

GtkTextInputManager::GtkTextInputManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

void GtkTextInputManager::update(RnView *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;
  entry.owner = this;

  const auto props = std::dynamic_pointer_cast<const TextInputProps>(shadowView.props);
  const bool multiline = props != nullptr && props->multiline;

  // Before the signals, because a field that switches between single and
  // multiline is a different widget and the old one's handlers go with it.
  GtkWidget *peer = rn_view_set_editable(view, TRUE, multiline ? TRUE : FALSE);
  const bool rebuilt = peer != entry.editable;
  entry.editable = peer;

  if (rebuilt) {

    GObject *signals = rn_peer_signal_source(entry.editable);
    g_signal_connect(signals, "changed", G_CALLBACK(onChanged), &entry);
    // "activate" is a GtkText signal and a single-line idea: Enter in a
    // multiline field inserts a newline, which is what SubmitBehavior::Newline
    // means and what BaseTextInputProps already reports for one.
    if (!multiline) {
      g_signal_connect(entry.editable, "activate", G_CALLBACK(onActivate), &entry);
    }

    // Both ends, because either can move on its own: an arrow key moves the
    // caret with the other end following, and shift-arrow moves one and not
    // the other. GtkText has no single "the selection changed" signal.
    g_signal_connect(signals, "notify::cursor-position",
                     G_CALLBACK(onSelectionChanged), &entry);
    // A GtkTextBuffer has no "selection-bound" property; it reports both ends
    // moving through "notify::has-selection" instead.
    g_signal_connect(signals,
                     multiline ? "notify::has-selection" : "notify::selection-bound",
                     G_CALLBACK(onSelectionChanged), &entry);

    // Capture phase, because onKeyPress has to fire *before* onChange -- which
    // means before GtkText has handled the key and changed the text. In the
    // bubble phase the edit has already happened and the two events arrive the
    // wrong way round.
    GtkEventController *keys = gtk_event_controller_key_new();
    gtk_event_controller_set_propagation_phase(keys, GTK_PHASE_CAPTURE);
    g_signal_connect(keys, "key-pressed", G_CALLBACK(onKeyPressed), &entry);
    gtk_widget_add_controller(entry.editable, keys);

    GtkEventController *focus = gtk_event_controller_focus_new();
    g_signal_connect(focus, "enter", G_CALLBACK(onFocusEnter), &entry);
    g_signal_connect(focus, "leave", G_CALLBACK(onFocusLeave), &entry);
    gtk_widget_add_controller(entry.editable, focus);
  }

  // Yoga has resolved border and padding into the content inset; the GtkText is
  // allocated inside it, so `paddingHorizontal` on a field means what it means
  // on a <View>.
  const auto &insets = shadowView.layoutMetrics.contentInsets;
  const GtkBorder border = {
      .left = static_cast<int16_t>(std::lround(insets.left)),
      .right = static_cast<int16_t>(std::lround(insets.right)),
      .top = static_cast<int16_t>(std::lround(insets.top)),
      .bottom = static_cast<int16_t>(std::lround(insets.bottom)),
  };
  rn_view_set_peer_insets(view, &border);

  if (props == nullptr || entry.editable == nullptr) {
    return;
  }

  // Controlled component: JavaScript owns the value. Three things have to be
  // true at once, and each fails differently.
  //
  // Setting it must not look like typing, or the change we report provokes a
  // re-render that sets it again and the two chase each other -- `applying`.
  //
  // A prop older than what the user has since typed must not be applied at all,
  // or a fast typist watches characters reorder themselves. That is what
  // React Native counts events for, and dropping such a value *without
  // recording it* is deliberate: the next render, once JavaScript has caught
  // up, applies it.
  //
  // And it is applied when the prop *changes*, not when it differs from the
  // widget, which is the only thing that tells a controlled field from an
  // uncontrolled one. See the header.
  const bool stale = props->mostRecentEventCount < entry.eventCount;
  const bool changed = !entry.sawProps || props->text != entry.lastPropText;
  if (changed && !stale) {
    entry.lastPropText = props->text;
    entry.sawProps = true;

    char *current = rn_peer_get_text(entry.editable);
    const bool differs = props->text != (current != nullptr ? current : "");
    g_free(current);
    if (differs) {
      entry.applying = true;
      // Preserve the cursor: assigning the text resets it to the start, which
      // sends the caret home on every keystroke of a controlled input.
      const int cursor = rn_peer_get_position(entry.editable);
      rn_peer_set_text(entry.editable, props->text.c_str());
      rn_peer_set_position(entry.editable,
                           MIN(cursor, static_cast<int>(props->text.size())));
      entry.applying = false;
      entry.lastReportedText = props->text;
    }
  }

  // A controlled *selection*, under the same staleness rule the text is under:
  // JavaScript that has not yet seen the last keystroke must not be allowed to
  // drag the caret back to where it thought it was.
  //
  // Applied when it changes rather than whenever it differs, for the reason
  // `text` is: a field that is merely uncontrolled sends no selection at all,
  // and re-asserting one every render would fight the user's own arrow keys.
  if (props->selection.has_value() && !stale) {
    const auto &selection = *props->selection;
    if (!entry.lastPropSelection.has_value() ||
        entry.lastPropSelection->start != selection.start ||
        entry.lastPropSelection->end != selection.end) {
      entry.lastPropSelection = selection;
      entry.applying = true;
      rn_peer_select_region(entry.editable, selection.start, selection.end);
      entry.applying = false;
      entry.lastReportedSelection =
          facebook::react::AttributedString::Range{selection.start, selection.end - selection.start};
    }
  }

  rn_peer_set_placeholder(entry.editable, props->placeholder.c_str());

  // GtkText renders in the GTK theme's colour and font, which has nothing to do
  // with the `style` this component was given -- on a dark field that is dark
  // text on dark. A PangoAttrList is how GtkText takes the style instead.
  //
  // fontSizeMultiplier is 1: nothing on this platform scales text for
  // accessibility settings yet, and passing 0 would multiply the size away.
  PangoAttrList *attributes = buildTextAttributes(props->getEffectiveTextAttributes(1.0F));
  rn_peer_set_attributes(entry.editable, attributes);
  pango_attr_list_unref(attributes);

  // `editable` is the prop; `readOnly` is the newer spelling of its inverse,
  // and React Native honours both.
  const bool writable = props->traits.editable && !props->readOnly;
  rn_peer_set_editable(entry.editable, writable ? TRUE : FALSE);

  rn_peer_set_visibility(entry.editable, props->traits.secureTextEntry ? FALSE : TRUE);

  // Zero means no limit, and so does the absurd default React Native uses when
  // the prop is absent -- passing that through would be a limit nobody asked
  // for on a field that had none.
  rn_peer_set_max_length(
      entry.editable,
      (props->maxLength > 0 && props->maxLength < 1000000) ? props->maxLength : 0);

  if (inserted && props->autoFocus) {
    gtk_widget_grab_focus(entry.editable);
  }
}

void GtkTextInputManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // The signal handlers hold a pointer to the Entry. Dropping the editable
  // first takes them with it.
  if (it->second.view != nullptr && RN_IS_VIEW(it->second.view)) {
    rn_view_set_editable(it->second.view, FALSE, FALSE);
  }
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// Events
// ---------------------------------------------------------------------------

std::shared_ptr<const TextInputEventEmitter> GtkTextInputManager::emitterFor(Tag tag) const {
  return std::dynamic_pointer_cast<const TextInputEventEmitter>(lookup_(tag));
}

TextInputEventEmitter::Metrics GtkTextInputManager::metricsFor(const Entry &entry) const {
  const char *text = entry.editable != nullptr
      ? rn_peer_get_text(entry.editable)
      : "";

  TextInputEventEmitter::Metrics metrics{};
  metrics.text = text != nullptr ? text : "";
  metrics.eventCount = entry.eventCount;
  metrics.target = entry.tag;

  // The whole selection, not only the caret. `get_selection_bounds` answers
  // FALSE when nothing is selected, and leaves the out parameters alone -- so
  // the caret position is the fallback and a zero length is the truth about it.
  int start = 0;
  int end = 0;
  if (entry.editable != nullptr &&
      !rn_peer_get_selection_bounds(entry.editable, &start, &end)) {
    start = end = rn_peer_get_position(entry.editable);
  }
  metrics.selectionRange = AttributedString::Range{start, end - start};

  // The scroll-shaped fields exist because iOS's text view is a scroll view.
  // Nothing here scrolls yet, so they describe a viewport the size of the
  // field, which is true and keeps JavaScript's arithmetic sane.
  const auto width = static_cast<facebook::react::Float>(
      entry.view != nullptr ? gtk_widget_get_width(GTK_WIDGET(entry.view)) : 0);
  const auto height = static_cast<facebook::react::Float>(
      entry.view != nullptr ? gtk_widget_get_height(GTK_WIDGET(entry.view)) : 0);
  metrics.containerSize = {.width = width, .height = height};
  metrics.contentSize = metrics.containerSize;
  metrics.layoutMeasurement = metrics.containerSize;
  metrics.zoomScale = 1.0F;

  return metrics;
}

void GtkTextInputManager::onChanged(GObject * /*source*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (entry->applying) {
    // This is a prop being applied, not the user typing.
    return;
  }

  char *text = rn_peer_get_text(entry->editable);
  const std::string value = text != nullptr ? text : "";
  if (value == entry->lastReportedText) {
    return;
  }
  entry->lastReportedText = value;
  entry->eventCount++;

  const auto emitter = entry->owner->emitterFor(entry->tag);
  if (emitter != nullptr) {
    emitter->onChange(entry->owner->metricsFor(*entry));
  }
}

void GtkTextInputManager::onActivate(GtkText * /*editable*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  const auto emitter = entry->owner->emitterFor(entry->tag);
  if (emitter != nullptr) {
    // Enter. React Native calls this submitEditing, and follows it with
    // endEditing on platforms where the field also gives up focus; GtkText
    // keeps focus on activate, so only the submit is reported.
    emitter->onSubmitEditing(entry->owner->metricsFor(*entry));
  }
}

gboolean GtkTextInputManager::onKeyPressed(GtkEventControllerKey * /*controller*/,
                                          guint keyval,
                                          guint /*keycode*/,
                                          GdkModifierType /*state*/,
                                          gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (entry == nullptr || entry->owner == nullptr) {
    return GDK_EVENT_PROPAGATE;
  }

  // React Native's contract: 'Enter' and 'Backspace' by name, and the typed
  // character otherwise -- including ' ' for space. Keys that produce no
  // character at all, the arrows and the modifiers, send nothing, which is what
  // iOS does too.
  std::string key;
  switch (keyval) {
    case GDK_KEY_Return:
    case GDK_KEY_KP_Enter:
    case GDK_KEY_ISO_Enter:
      key = "Enter";
      break;
    case GDK_KEY_BackSpace:
      key = "Backspace";
      break;
    default: {
      const gunichar character = gdk_keyval_to_unicode(keyval);
      if (character == 0 || !g_unichar_isprint(character)) {
        return GDK_EVENT_PROPAGATE;
      }
      char utf8[7] = {0};
      const gint length = g_unichar_to_utf8(character, utf8);
      key.assign(utf8, static_cast<size_t>(length));
      break;
    }
  }

  if (auto emitter = entry->owner->emitterFor(entry->tag)) {
    TextInputEventEmitter::KeyPressMetrics metrics{};
    metrics.text = key;
    metrics.eventCount = entry->eventCount;
    emitter->onKeyPress(metrics);
  }

  // Never handled here: this observes the key on its way to GtkText, which
  // still has to do the editing.
  return GDK_EVENT_PROPAGATE;
}

void GtkTextInputManager::onSelectionChanged(GObject * /*object*/,
                                            GParamSpec * /*pspec*/,
                                            gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (entry == nullptr || entry->owner == nullptr) {
    return;
  }
  // Not while a prop is being pushed in. Applying `text` moves the caret, and
  // reporting that as the user selecting something would make a controlled
  // field fight its own render.
  if (entry->applying) {
    return;
  }

  const auto metrics = entry->owner->metricsFor(*entry);
  // Both signals fire for one movement, so this collapses the pair into the
  // one event JavaScript should see.
  if (metrics.selectionRange.location == entry->lastReportedSelection.location &&
      metrics.selectionRange.length == entry->lastReportedSelection.length) {
    return;
  }
  entry->lastReportedSelection = metrics.selectionRange;

  if (auto emitter = entry->owner->emitterFor(entry->tag)) {
    emitter->onSelectionChange(metrics);
  }
}

void GtkTextInputManager::onFocusEnter(GtkEventControllerFocus * /*controller*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  const auto emitter = entry->owner->emitterFor(entry->tag);
  if (emitter != nullptr) {
    emitter->onFocus(entry->owner->metricsFor(*entry));
  }
}

void GtkTextInputManager::onFocusLeave(GtkEventControllerFocus * /*controller*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  const auto emitter = entry->owner->emitterFor(entry->tag);
  if (emitter != nullptr) {
    emitter->onBlur(entry->owner->metricsFor(*entry));
    emitter->onEndEditing(entry->owner->metricsFor(*entry));
  }
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool GtkTextInputManager::dispatchCommand(Tag tag,
                                          const std::string &name,
                                          const folly::dynamic &args) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;
  if (entry.editable == nullptr) {
    return false;
  }

  if (name == "focus") {
    gtk_widget_grab_focus(GTK_WIDGET(entry.editable));
    return true;
  }

  if (name == "blur") {
    // GTK has no "unfocus this widget"; focus moves, it is not dropped. Handing
    // it to the window's default is the closest thing, and it makes the focus
    // controller report a leave, so JavaScript still sees onBlur.
    GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(entry.editable));
    if (root != nullptr) {
      gtk_widget_grab_focus(GTK_WIDGET(root));
    }
    return true;
  }

  if (name == "setTextAndSelection") {
    // [eventCount, text, start, end]. An eventCount older than what the user
    // has since typed means this command is stale and must be dropped, which is
    // the whole reason React Native counts them.
    if (args.isArray() && args.size() >= 2) {
      const int eventCount = static_cast<int>(args[0].asInt());
      if (eventCount < entry.eventCount) {
        return true;
      }
      entry.applying = true;
      const auto text = args[1].isString() ? args[1].asString() : std::string{};
      rn_peer_set_text(entry.editable, text.c_str());
      if (args.size() >= 4 && args[2].isInt()) {
        const int start = static_cast<int>(args[2].asInt());
        const int end = static_cast<int>(args[3].asInt());
        rn_peer_select_region(entry.editable, start, end);
      }
      entry.applying = false;
      entry.lastReportedText = text;
    }
    return true;
  }

  return false;
}

} // namespace basalt
