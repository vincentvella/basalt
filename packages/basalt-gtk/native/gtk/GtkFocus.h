// Keyboard focus, for everything that is not a <TextInput>.
//
// A text field takes focus because it is a real GtkText and GTK gives it focus;
// nothing else in a React Native tree was reachable by Tab at all, and an
// application whose buttons cannot be reached from the keyboard is not really a
// desktop application.
//
// ## What makes a view focusable
//
// `accessible`. React Native has a `focusable` prop and it does not reach this
// platform: ReactCommon parses it only into Android's and tvOS's
// `HostPlatformViewProps`, and the C++ host's is a bare alias of
// `BaseViewProps`. So the signal has to be something that does arrive, and
// `accessible` is both the nearest true thing and the right one -- it is what
// `<Pressable>` sets on everything it renders, what an app sets on anything
// else it means as a control, and what a screen reader already stops on. Tab
// order following the accessibility tree is also what GTK and macOS do with
// their own widgets.
//
// ## What activating one does
//
// Dispatches `topClick` with an empty payload, which is exactly what React
// Native for Android does from `ReactViewManager.setFocusable`. Pressability
// listens for it and calls `onPress`, but only when the payload has no
// `pointerType` -- a click that came from a pointer would fire `onPress` twice.
// So this cannot go through `TouchEventEmitter::onClick`, which always carries
// one; it is `dispatchEvent("click", folly::dynamic::object())`, the same shape
// Android's ViewGroupClickEvent has.
//
// Enter and space, because those are the two keys that activate a control on
// every desktop and in the browser.
//
// ## What GTK contributes
//
// The chain. A focusable widget joins the window's focus order, so Tab and
// Shift+Tab already work and nothing here decides what "next" means -- which
// also means a <TextInput>'s peer sits in the same order as the buttons around
// it, without this file knowing that text inputs exist. The Win32 host has no
// such chain and has to build one; that is the difference between the two, and
// it is GTK's to take credit for.

#pragma once

#include "KeyEvents.h"

#include "GtkMountingManager.h"
#include "RnView.h"

namespace basalt {

class GtkFocusManager {
 public:
  GtkFocusManager(GtkMountingManager *mountingManager, RnView *surfaceRoot);
  ~GtkFocusManager();

  GtkFocusManager(const GtkFocusManager &) = delete;
  GtkFocusManager &operator=(const GtkFocusManager &) = delete;
  GtkFocusManager(GtkFocusManager &&) = delete;
  GtkFocusManager &operator=(GtkFocusManager &&) = delete;

  // Moves focus to the next or previous focusable view, and reports whether
  // anything took it. The keyboard's own Tab goes through GTK; this is the
  // entry point automation uses, for the same reason `synthesiseTap` exists --
  // a real Tab needs a window the display server considers focused, which a
  // headless run does not have.
  bool moveFocus(bool forward);

  // Activates whatever has focus, as Enter or space does.
  bool activateFocused();

  // The tag of the focused view, or 0. For tests and for the tree dump.
  facebook::react::Tag focusedTag() const;

  // A key press. Answers whether some view from the focused one outwards declared
  // this combination, and reports it when one did. The answer comes back in this
  // call stack because `key-pressed` consumes or propagates by it; see
  // core/KeyEvents.h.
  bool handleKey(guint keyval, GdkModifierType state);

  // BASALT_TEST_KEY. The same delivery a real press gets, minus the keyval
  // translation, which tests/test_key_events_gtk.cpp covers instead. See
  // core/TestKeys.h.
  bool deliverTestKey(const KeyCombination &pressed);

 private:
  static gboolean onKeyPressed(GtkEventControllerKey *controller,
                               guint keyval,
                               guint keycode,
                               GdkModifierType state,
                               gpointer userData);
  static void onFocusChanged(GObject *window, GParamSpec *spec, gpointer userData);
  static void onRootChanged(GObject *widget, GParamSpec *spec, gpointer userData);

  // Connects to the window's focus-widget notification. One signal for the
  // whole tree, rather than one per view: views come and go with every
  // mutation, and the window does not.
  void attachToWindow();

  // Works out which React Native view holds focus now and reports the change.
  void refreshFocus();

  void emitFocus(facebook::react::Tag tag, bool focused);

  GtkMountingManager *mountingManager_;
  // The surface root, held weakly: GObject nulls this when the view is
  // finalised, which is what makes the destructor safe to run after the root
  // has gone. A type check cannot do that job -- see the destructor.
  RnView *surfaceRoot_;
  GtkEventController *keyController_{nullptr};
  // The window this is listening to, held weakly: it outlives the manager in
  // the normal case and does not during teardown.
  GtkWindow *window_{nullptr};
  facebook::react::Tag focusedTag_{0};
};

} // namespace basalt
