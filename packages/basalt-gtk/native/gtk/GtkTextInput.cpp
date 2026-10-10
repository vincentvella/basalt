#include "GtkTextInput.h"

#include "FontScaling.h"
#include "TextChecking.h"

#include "GtkTextPeer.h"

#include "PangoTextLayout.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/textinput/TextInputEventEmitter.h>
#include <react/renderer/graphics/Color.h>

#include <cmath>
#include <cstdint>
#include <optional>
#include <string_view>

namespace basalt {

using facebook::react::AttributedString;
using facebook::react::ShadowView;
using facebook::react::SharedColor;
using facebook::react::Tag;
using facebook::react::TextInputEventEmitter;
using facebook::react::TextInputProps;

namespace {

// A React Native colour as the GdkRGBA the peer seam wants, or nothing when the
// prop was absent.
//
// Nothing rather than transparent, and nothing rather than black: SharedColor's
// unset value *is* zero, so handing it on as a colour would paint a caret black
// on every field that never asked for one. The optional is what carries the
// difference across to rn_peer_set_colors, which takes a null pointer per
// colour for exactly this reason.
std::optional<GdkRGBA> toRgba(const SharedColor &color) {
  if (!color) {
    return std::nullopt;
  }
  const auto components = facebook::react::colorComponentsFromColor(color);
  GdkRGBA rgba;
  rgba.red = components.red;
  rgba.green = components.green;
  rgba.blue = components.blue;
  rgba.alpha = components.alpha;
  return rgba;
}

const GdkRGBA *orNull(const std::optional<GdkRGBA> &colour) {
  return colour.has_value() ? &*colour : nullptr;
}

} // namespace

GtkTextInputManager::GtkTextInputManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {}

// ---------------------------------------------------------------------------
// Mutations
// ---------------------------------------------------------------------------

namespace {

// React Native's fourteen keyboard types against GTK's eleven input purposes.
//
// A purpose is what the text is *for*, which is the honest half of
// `keyboardType` on a desktop: there is no on-screen keyboard to choose a
// layout for, and an input method reads the purpose to decide what to offer. On
// a Linux tablet the purpose is what brings up a number pad.
//
// Several of React Native's types have no purpose to map to and fall to
// `FREE_FORM`, which is GTK's "ordinary text" rather than a refusal:
// `ascii-capable` and `numbers-and-punctuation` describe a *keyboard* rather
// than a kind of text, `twitter` and `web-search` are iOS keyboards with no
// equivalent idea here, and `visible-password` is Android's password that shows
// -- GTK's `PASSWORD` purpose is for one that does not, and `secureTextEntry`
// is the prop that makes a field secure here.
GtkInputPurpose toInputPurpose(facebook::react::KeyboardType type) {
  switch (type) {
    case facebook::react::KeyboardType::EmailAddress:
      return GTK_INPUT_PURPOSE_EMAIL;
    case facebook::react::KeyboardType::URL:
      return GTK_INPUT_PURPOSE_URL;
    case facebook::react::KeyboardType::PhonePad:
    case facebook::react::KeyboardType::NamePhonePad:
      return GTK_INPUT_PURPOSE_PHONE;
    // Digits only, which is what a number pad offers.
    case facebook::react::KeyboardType::NumberPad:
    case facebook::react::KeyboardType::ASCIICapableNumberPad:
      return GTK_INPUT_PURPOSE_DIGITS;
    // A number, which allows a decimal separator and a sign where DIGITS does
    // not -- so `numeric` and `decimal-pad` are this and not that.
    case facebook::react::KeyboardType::Numeric:
    case facebook::react::KeyboardType::DecimalPad:
      return GTK_INPUT_PURPOSE_NUMBER;
    case facebook::react::KeyboardType::Default:
    case facebook::react::KeyboardType::ASCIICapable:
    case facebook::react::KeyboardType::NumbersAndPunctuation:
    case facebook::react::KeyboardType::Twitter:
    case facebook::react::KeyboardType::WebSearch:
    case facebook::react::KeyboardType::VisiblePassword:
      break;
  }
  return GTK_INPUT_PURPOSE_FREE_FORM;
}

} // namespace

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
  // 1, and not the desktop's text scale, although this is the argument React
  // Native leaves for one: getEffectiveTextAttributes would put it in
  // `fontSizeMultiplier`, and buildTextAttributes applies the scale itself
  // through core/FontScaling.h -- so passing it here would square it. The field
  // still scales, and still stops scaling when `allowFontScaling={false}`.
  PangoAttrList *attributes = buildTextAttributes(props->getEffectiveTextAttributes(1.0F));
  rn_peer_set_attributes(entry.editable, attributes);
  pango_attr_list_unref(attributes);

  // The three colours that attribute list cannot carry, because GTK takes them
  // from CSS: the placeholder, the selection and the caret. See
  // rn_peer_set_colors.
  //
  // `cursorColor` falls back to `selectionColor`, which is what React Native
  // documents rather than something invented here: `selectionColor` is "the
  // highlight, selection handle and cursor color of the text input", and
  // `cursorColor` exists to override the caret on its own. A field given only
  // `selectionColor` should therefore have a caret in that colour, not the
  // theme's, and iOS gets the same effect by both of them being the view's
  // tintColor.
  const std::optional<GdkRGBA> placeholderColor = toRgba(props->placeholderTextColor);
  const std::optional<GdkRGBA> selectionColor = toRgba(props->selectionColor);
  std::optional<GdkRGBA> cursorColor =
      props->cursorColor ? toRgba(props->cursorColor) : selectionColor;

  // `caretHidden`, which goes in through the same channel and wins over both of
  // the above: a caret the app asked to hide is hidden whatever colour it also
  // asked for. Fully transparent rather than absent -- absent means "the
  // theme's", which is a visible caret.
  //
  // GTK has no property for this and does not need one: `caret-color` takes an
  // alpha, which is also how a browser hides one.
  if (props->traits.caretHidden) {
    cursorColor = GdkRGBA{0.0F, 0.0F, 0.0F, 0.0F};
  }
  rn_peer_set_colors(
      entry.editable, orNull(placeholderColor), orNull(selectionColor), orNull(cursorColor));

  // `contextMenuHidden`, which GTK has no property for: a capture-phase gesture
  // claims the secondary click before the peer's own menu sees it. The keyboard
  // route is not covered on this host; see rn_peer_set_context_menu_hidden.
  rn_peer_set_context_menu_hidden(entry.editable,
                                  props->traits.contextMenuHidden ? TRUE : FALSE);

  // `editable` is the prop; `readOnly` is the newer spelling of its inverse,
  // and React Native honours both.
  const bool writable = props->traits.editable && !props->readOnly;
  rn_peer_set_editable(entry.editable, writable ? TRUE : FALSE);

  rn_peer_set_visibility(entry.editable, props->traits.secureTextEntry ? FALSE : TRUE);

  // Zero means no limit, and so does the absurd default React Native uses when
  // the prop is absent -- passing that through would be a limit nobody asked
  // for on a field that had none.
  // The peer takes an int rather than core's enum, its header being plain C
  // against GTK, so the two have to agree about which number means what.
  static_assert(static_cast<int>(basalt::TextCheckingFlag::Unset) == 0);
  static_assert(static_cast<int>(basalt::TextCheckingFlag::On) == 1);
  static_assert(static_cast<int>(basalt::TextCheckingFlag::Off) == 2);
  // And the capitalisation order the peer's ints rely on, for the same reason.
  static_assert(static_cast<int>(facebook::react::AutocapitalizationType::None) == 0);
  static_assert(static_cast<int>(facebook::react::AutocapitalizationType::Words) == 1);
  static_assert(static_cast<int>(facebook::react::AutocapitalizationType::Sentences) == 2);
  static_assert(static_cast<int>(facebook::react::AutocapitalizationType::Characters) == 3);
  // `spellCheck`, resolved in core/TextChecking.h so this host and AppKit agree
  // that unset is not false. `autoCorrect` has no GTK hint and is recorded
  // rather than approximated; see the peer's header.
  const auto spellCheck = basalt::textCheckingFlag(props->traits.spellCheck);
  const auto autoCorrect = basalt::textCheckingFlag(props->traits.autoCorrect);
  rn_peer_set_spell_check(entry.editable, static_cast<int>(spellCheck));
  // And both in the dump, as the app asked for them: this host honours only the
  // first, and the line is what the two hosts compare on.
  rn_view_set_text_checking(
      view, basalt::textCheckingName(spellCheck), basalt::textCheckingName(autoCorrect));

  // `autoCapitalize` and `keyboardType`, which this host can express and AppKit
  // cannot: three more input hints and an input purpose. Both are plain enums
  // with no "did not say", and React Native's default for the first is
  // `sentences` -- iOS's -- so a hint is always set.
  rn_peer_set_auto_capitalize(entry.editable,
                              static_cast<int>(props->traits.autocapitalizationType));
  rn_peer_set_input_purpose(entry.editable,
                            static_cast<int>(toInputPurpose(props->traits.keyboardType)));
  rn_view_set_input_kinds(view,
                          basalt::autoCapitalizeName(props->traits.autocapitalizationType),
                          basalt::keyboardTypeName(props->traits.keyboardType));
  // And the two hiding props, for the dump: each is the absence of something,
  // so the dump is the only place anything can see that they arrived.
  rn_view_set_input_hiding(view,
                           props->traits.caretHidden ? TRUE : FALSE,
                           props->traits.contextMenuHidden ? TRUE : FALSE);

  // `clearTextOnFocus` and `selectTextOnFocus`, remembered for the moment focus
  // arrives. See onFocusEnter.
  entry.clearTextOnFocus = props->traits.clearTextOnFocus;
  entry.selectTextOnFocus = props->traits.selectTextOnFocus;

  // `submitBehavior`, resolved by React Native rather than here: `Default` is
  // `newline` for a multiline field and `blurAndSubmit` for a single-line one.
  entry.submitBehavior = props->getNonDefaultSubmitBehavior();
  entry.multiline = props->multiline;

  rn_peer_set_max_length(
      entry.editable,
      (props->maxLength > 0 && props->maxLength < 1000000) ? props->maxLength : 0);

  if (inserted && props->autoFocus) {
    // Recorded, not performed: see flushAutoFocus in the header for why here is
    // too early. `inserted` is the only moment the prop can be acted on at all,
    // being the only moment this manager can tell a new widget from an updated
    // one, so the tag is kept and the focusing happens later.
    pendingAutoFocus_.push_back(tag);
  }
}

void GtkTextInputManager::flushAutoFocus() {
  if (pendingAutoFocus_.empty()) {
    return;
  }
  const std::vector<facebook::react::Tag> pending = std::move(pendingAutoFocus_);
  pendingAutoFocus_.clear();

  for (const facebook::react::Tag tag : pending) {
    const auto it = entries_.find(tag);
    if (it == entries_.end()) {
      continue;
    }
    GtkWidget *const widget = GTK_WIDGET(it->second.editable);

    // Mapped first, because focusing a widget that is not on screen is wrong on
    // its own terms, even though it turned out not to be what hung the host.
    if (gtk_widget_get_mapped(widget)) {
      grabFocusForAutoFocus(widget);
      continue;
    }

    // After the default handler, so the widget is mapped by the time this runs
    // rather than merely about to be. The handler dies with the widget, so a
    // field that is removed before it is ever shown needs no cleanup here.
    g_signal_connect_after(widget, "map", G_CALLBACK(focusWhenMapped), nullptr);
  }
}

void GtkTextInputManager::focusWhenMapped(GtkWidget *widget, gpointer /*userData*/) {
  // Once. Without this, hiding and showing the field again would steal focus
  // back, which is not what autoFocus means.
  g_signal_handlers_disconnect_by_func(
      widget, reinterpret_cast<gpointer>(focusWhenMapped), nullptr);
  grabFocusForAutoFocus(widget);
}

// `gtk_widget_grab_focus` is the wrong call for a single-line field, and this is
// what hung the host rather than anything to do with mapping. GtkText treats a
// programmatic focus as keyboard focus and selects all of its text, selected
// text is an X11 PRIMARY selection, and claiming one needs a server timestamp:
// `gdk_x11_get_server_time` does an `XChangeProperty` and then blocks in
// `XIfEvent` with no timeout. Under the display CI runs on that round trip does
// not come back, inside a mount, so the main loop never runs again. See
// docs/backlog/testing.md.
//
// It is also the better behaviour. autoFocus is meant to put a caret in a field,
// not to select what is already in it and have the next keystroke replace it.
void GtkTextInputManager::grabFocusForAutoFocus(GtkWidget *widget) {
  // Multiline is a GtkTextView, which does not select on focus and has no
  // equivalent call, so it keeps the ordinary one.
  if (!rn_peer_is_multiline(widget) && GTK_IS_TEXT(widget)) {
    gtk_text_grab_focus_without_selecting(GTK_TEXT(widget));
    return;
  }
  gtk_widget_grab_focus(widget);
}

void GtkTextInputManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // The signal handlers hold a pointer to the Entry. Dropping the editable
  // first takes them with it.
  // Null rather than RN_IS_VIEW, for the reason core/MountingWalk.h gives: the
  // view is alive by the time this runs, and a type check could not tell if it
  // were not.
  if (it->second.view != nullptr) {
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
  if (entry == nullptr || entry->owner == nullptr) {
    return;
  }
  // Enter in a single-line field. GtkText keeps focus on activate, so whether
  // the field gives it up is `submitBehavior`'s to say rather than the
  // toolkit's -- which is what this used to leave to the toolkit.
  entry->owner->handleSubmitKey(*entry);
}

void GtkTextInputManager::dropFocus(Entry &entry) {
  if (entry.editable == nullptr) {
    return;
  }
  GtkRoot *root = gtk_widget_get_root(GTK_WIDGET(entry.editable));
  if (GTK_IS_WINDOW(root)) {
    gtk_window_set_focus(GTK_WINDOW(root), nullptr);
  } else if (root != nullptr) {
    // Not in a window: a surface root during a test, or a widget not yet shown.
    // Moving focus to the root is the only thing left.
    gtk_widget_grab_focus(GTK_WIDGET(root));
  }
}

void GtkTextInputManager::handleSubmitKey(Entry &entry) {
  // **`submitBehavior` decides what Enter does**, already resolved to one of the
  // three: `newline` inserts one, `submit` reports a submit, and
  // `blurAndSubmit` reports one and gives up focus. React Native's default is
  // the last for a single-line field and the first for a multiline one, so a
  // field that never set the prop behaves as it does on iOS.
  if (entry.submitBehavior == facebook::react::SubmitBehavior::Newline) {
    return;
  }
  if (const auto emitter = emitterFor(entry.tag)) {
    emitter->onSubmitEditing(metricsFor(entry));
  }
  if (entry.submitBehavior == facebook::react::SubmitBehavior::BlurAndSubmit) {
    dropFocus(entry);
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

  // Enter in a *multiline* field, which is the one case this controller has to
  // act on rather than observe: a GtkTextView inserts the newline itself, so
  // `submit` and `blurAndSubmit` have to stop the key here. A GtkText reaches
  // the same code through `activate`, which is a signal rather than a key.
  if (key == "Enter" && entry->multiline
      && entry->submitBehavior != facebook::react::SubmitBehavior::Newline) {
    entry->owner->handleSubmitKey(*entry);
    return GDK_EVENT_STOP;
  }

  // Otherwise never handled here: this observes the key on its way to the peer,
  // which still has to do the editing.
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
  entry->owner->handleFocus(entry->tag);
}

void GtkTextInputManager::handleFocus(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  Entry *const entry = &it->second;
  const auto emitter = emitterFor(entry->tag);
  if (emitter != nullptr) {
    emitter->onFocus(metricsFor(*entry));
  }

  // `clearTextOnFocus` before `selectTextOnFocus`, which is the only order that
  // means anything: selecting what has just been cleared selects nothing, and
  // clearing what has just been selected throws the selection away. All three
  // hosts do them in this order.
  if (entry->clearTextOnFocus) {
    char *existing = rn_peer_get_text(entry->editable);
    const bool hadText = existing != nullptr && *existing != '\0';
    g_free(existing);
    if (hadText) {
      // Reported rather than applied silently, through the same path a
      // keystroke takes: a controlled field cleared behind JavaScript's back is
      // a field that puts the old value back on the next unrelated render, with
      // the user having done nothing.
      // `gtk_editable_set_text` emits "changed", which is the signal this
      // manager already reports from, so the clear is reported for free -- and
      // `onChanged` is called here as well for the same reason the Windows half
      // does it: it compares against what was last reported, so the belt and
      // the brace cost one string comparison.
      rn_peer_set_text(entry->editable, "");
      onChanged(nullptr, entry);
    }
  }

  if (entry->selectTextOnFocus) {
    // Everything, which `rn_peer_select_region` spells with -1 as its end.
    //
    // Selecting text on *this* host is what `grabFocusForAutoFocus` goes out of
    // its way to avoid, and the reason is worth knowing before anyone moves
    // this: `gtk_widget_grab_focus` on a GtkText selects all of its text, which
    // claims the X11 PRIMARY selection, which needs a server timestamp -- and
    // that round trip hung the host when it happened *inside a mount*. Here it
    // is a focus-in signal with the main loop already running, which is also
    // where the controlled `selection` prop selects from, so there is nothing
    // blocking the reply. See backlog/textinput.md.
    rn_peer_select_region(entry->editable, 0, -1);
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
    // Dropped, not moved. `gtk_window_set_focus` with NULL "unsets the focus
    // widget for the root" in GTK's own words, which is what `blur` means on
    // every other platform: nothing is focused afterwards.
    //
    // This used to hand focus to the root instead, with a comment saying GTK has
    // no way to unfocus a widget and that focus moves rather than being dropped.
    // That is not true, and the difference was visible: the window itself became
    // the focus widget, so a Tab after a blur started from the window rather than
    // from the top of the tab order, and anything asking what had focus got the
    // window instead of nothing.
    //
    // Either way the focus controller reports a leave, so JavaScript sees onBlur,
    // which is why the old behaviour looked right from the app's side.
    dropFocus(entry);
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
