// <TextInput> on GTK4.
//
// The native half is React Native's own *iOS* TextInput: its shadow node,
// props, state and event emitter are pure C++ and measure through a
// TextLayoutManager, which on this platform is the Pango one. Android's variant
// includes fbjni and calls into a Java FabricUIManager, so it is unusable here;
// see docs/DECISIONS.md. The component name is therefore "TextInput", which is
// what packages/basalt-core' src/overrides/TextInput.js asks for.
//
// The editing itself is a real GtkText -- the widget behind GtkEntry -- rather
// than a cursor drawn on a PangoLayout. That brings input methods, selection,
// the clipboard, and every keybinding a Linux user expects, none of which is
// worth reimplementing and all of which is easy to get subtly wrong.
//
// The awkward part of a text field is not typing, it is that React Native's
// <TextInput> is a controlled component: JavaScript owns the value, and the
// widget must not fight it. Every `changed` signal is reported to JavaScript,
// which re-renders and sends the text back down as a prop -- so applying that
// prop must not itself look like the user typing, or the two chase each other.
// `applying_` is what breaks that loop.

#pragma once

#include "RnView.h"

#include <react/renderer/components/iostextinput/TextInputShadowNode.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/mounting/ShadowView.h>

#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>

namespace basalt {

class GtkTextInputManager {
 public:
  using EmitterLookup = std::function<facebook::react::EventEmitter::Shared(facebook::react::Tag)>;

  explicit GtkTextInputManager(EmitterLookup lookup);

  // Called for every mutation touching a TextInput. Creates the editable the
  // first time, then applies props.
  void update(RnView *view, const facebook::react::ShadowView &shadowView);

  void remove(facebook::react::Tag tag);

  // Focuses whatever `autoFocus` asked for, once the transaction is on screen.
  //
  // It cannot be done where the prop is read. Props are applied by
  // MountingWalk::create, immediately after `createView` and before any Insert,
  // so the widget has no parent and therefore no GtkRoot, and
  // `gtk_widget_grab_focus` has nothing to set focus on. Deferred to the end of
  // the transaction, which is where resolveRefreshControls already lives for the
  // same reason.
  //
  // **The end of the transaction is still not late enough**, which cost a day
  // to find. A parent is not a window on screen: the widget may be unmapped
  // until GTK's next layout pass, and focusing a GtkText that is not mapped
  // takes GTK into claiming the PRIMARY selection against a surface that cannot
  // answer, where `gdk_x11_get_server_time` blocks in `XIfEvent` with no
  // timeout and the main loop never runs again. That is the stall in
  // docs/backlog/testing.md, and it was this. So a widget that is not yet
  // mapped is focused from its own `map` signal instead.
  void flushAutoFocus();

  // focus, blur, and setTextAndSelection. Returns false for anything else.
  bool dispatchCommand(facebook::react::Tag tag,
                       const std::string &name,
                       const folly::dynamic &args);

 private:
  // Set when a widget is created with `autoFocus`, cleared by flushAutoFocus.
  std::vector<facebook::react::Tag> pendingAutoFocus_;

  struct Entry {
    RnView *view{nullptr};
    // A GtkText or a GtkTextView; see GtkTextPeer.h.
    GtkWidget *editable{nullptr};
    facebook::react::Tag tag{0};
    GtkTextInputManager *owner{nullptr};

    // React Native counts events so it can ignore a prop update that is older
    // than what the user has since typed. Every emitted metric carries it.
    int eventCount{0};

    // True while a prop is being pushed into the widget, so the `changed`
    // signal it provokes is not reported back as the user typing.
    bool applying{false};

    std::string lastReportedText;

    // The selection as JavaScript last saw it. GtkText fires two notifies for
    // one movement, so this is what keeps that from being two events.
    facebook::react::AttributedString::Range lastReportedSelection{0, 0};

    // The last `selection` prop seen, for the same reason lastPropText exists:
    // applying on change rather than on difference is what leaves an
    // uncontrolled field's own caret alone.
    std::optional<facebook::react::Selection> lastPropSelection{};

    // The last `text` prop actually seen, and whether one has been seen at all.
    //
    // This is what tells a *controlled* field from an uncontrolled one, which
    // props alone cannot: React Native's TextInput.js sends
    // `text={value ?? defaultValue}`, and an uncontrolled field with no default
    // sends undefined, which arrives here as the empty string -- exactly what a
    // controlled field that JavaScript has cleared sends. Applying it whenever
    // it differs from the widget therefore wipes an uncontrolled field the
    // moment anything else in the tree re-renders, because React Native
    // re-sends `mostRecentEventCount` on every change and that alone produces
    // an Update mutation.
    //
    // So the prop is applied when it *changes*, not when it differs from the
    // widget. A controlled field's value changes as the user types; an
    // uncontrolled one's never does. Found on Windows.
    std::string lastPropText;
    bool sawProps{false};
  };

  // The sender differs by peer -- the widget for a GtkText, the buffer for a
  // GtkTextView -- so it arrives untyped and goes unused.
  static void onChanged(GObject *source, gpointer userData);
  static void onActivate(GtkText *editable, gpointer userData);
  static gboolean onKeyPressed(GtkEventControllerKey *controller,
                               guint keyval,
                               guint keycode,
                               GdkModifierType state,
                               gpointer userData);
  static void onSelectionChanged(GObject *object, GParamSpec *pspec, gpointer userData);
  static void onFocusEnter(GtkEventControllerFocus *controller, gpointer userData);
  static void onFocusLeave(GtkEventControllerFocus *controller, gpointer userData);

  // Focuses an `autoFocus` widget that was not mapped when the transaction
  // ended, then disconnects itself: autoFocus is the first chance, not every
  // time the widget is shown again.
  static void focusWhenMapped(GtkWidget *widget, gpointer userData);

  // Focuses without selecting, where the peer has a call for it. See the
  // definition: selecting on focus is what claimed an X11 PRIMARY selection and
  // blocked the main loop.
  static void grabFocusForAutoFocus(GtkWidget *widget);

  // Fills in the parts of Metrics every event carries.
  facebook::react::TextInputEventEmitter::Metrics metricsFor(const Entry &entry) const;

  std::shared_ptr<const facebook::react::TextInputEventEmitter> emitterFor(
      facebook::react::Tag tag) const;

  EmitterLookup lookup_;
  std::unordered_map<facebook::react::Tag, Entry> entries_;
};

} // namespace basalt
