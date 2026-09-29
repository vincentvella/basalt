// Keyboard focus, for everything that is not a <TextInput>.
//
// Deliberately the same shape as react-native-basalt-gtk's GtkFocusManager,
// down to what makes a view focusable and what activating one dispatches,
// because none of that is about the toolkit: if the two diverge, an app's
// buttons will be reachable from the keyboard on one desktop and not the other.
//
// ## What makes a view focusable
//
// `accessible`. React Native has a `focusable` prop and it does not reach this
// platform: ReactCommon parses it only into Android's and tvOS's
// `HostPlatformViewProps`, and the C++ host's is a bare alias of
// `BaseViewProps`. So the signal has to be something that does arrive, and
// `accessible` is both the nearest true thing and the right one -- it is what
// `<Pressable>` sets on everything it renders, and what a screen reader already
// stops on.
//
// ## What activating one does
//
// Dispatches `topClick` with an empty payload, which is what React Native for
// Android does from `ReactViewManager.setFocusable`. Pressability listens for
// it and calls `onPress`, but only when the payload has no `pointerType` -- a
// click that came from a pointer would fire `onPress` twice. So this cannot go
// through `TouchEventEmitter::onClick`, which always carries one.
//
// ## The chain, which AppKit does not contribute
//
// This is the one place the two desktops genuinely differ. GTK hands over its
// focus chain: a focusable widget joins the window's order and Tab works. AppKit
// has a key-view loop and it is not usable here -- `selectNextKeyView:` needs
// `nextKeyView` links or `autorecalculatesKeyViewLoop`, both of which assume a
// window built from a nib, and a programmatically built one with neither simply
// does nothing. Which is exactly what it did: seven Tab presses, no focus.
//
// So the order is walked here, in tree order, which is what GTK's chain
// produces anyway. A <TextInput> is a stop like any other: the responder for
// one is its NSTextField peer rather than the view, because that is the thing
// AppKit will make first responder.

#pragma once

#include "KeyEvents.h"

#import "RnAppKitView.h"

#include "AppKitMountingManager.h"

namespace basalt {

class AppKitFocusManager {
 public:
  AppKitFocusManager(AppKitMountingManager *mountingManager, RnAppKitView *surfaceRoot);
  ~AppKitFocusManager();

  AppKitFocusManager(const AppKitFocusManager &) = delete;
  AppKitFocusManager &operator=(const AppKitFocusManager &) = delete;
  AppKitFocusManager(AppKitFocusManager &&) = delete;
  AppKitFocusManager &operator=(AppKitFocusManager &&) = delete;

  // Moves focus to the next or previous focusable view, and reports whether
  // anything took it. The keyboard's own Tab goes through AppKit; this is the
  // entry point automation uses, for the same reason `synthesiseTap` exists --
  // a real Tab needs a window the window server considers key, which an
  // automated run does not reliably have.
  bool moveFocus(bool forward);

  // Activates whatever has focus, as Enter or space does.
  bool activateFocused();

  // A key press arriving at `view`, which is the first responder. Answers whether
  // some view from there outwards declared this combination -- and reports it to
  // JavaScript when one did. The answer has to come back in this call stack,
  // because the caller consumes the key or passes it on by it; see
  // core/KeyEvents.h.
  bool handleKey(RnAppKitView *view, NSEvent *event);

  // BASALT_TEST_KEY. The same delivery a real press gets, minus the NSEvent
  // translation -- which is the half this cannot exercise and which
  // tests/test_appkit_key_events.mm covers instead. See core/TestKeys.h.
  bool deliverTestKey(const KeyCombination &pressed);

  // A real key press that the responder chain was never going to bring here.
  //
  // `handleKey` above is reached from `RnAppKitView keyDown:`, which AppKit only
  // calls on the first responder. An app whose shortcut holder is the root view
  // has no first responder inside the React tree at all -- nothing focuses it,
  // `focusable` only makes a view eligible -- so the press goes to the window
  // and is gone. Every shortcut in such an app is dead, which is what kino was.
  //
  // Called from the window's key monitor for exactly that case, and only that
  // case: with a text field or any other view focused, the responder chain does
  // reach `handleKey` and this must not run as well.
  bool deliverWindowKey(NSEvent *event);

  // The tag of the focused view, or 0. For tests and for automation.
  facebook::react::Tag focusedTag() const {
    return focusedTag_;
  }

  // The main-thread halves, called by the Objective-C trampoline.
  void viewDidChangeFocus(RnAppKitView *view, bool focused);
  bool activate(RnAppKitView *view);

 private:
  void emitFocus(facebook::react::Tag tag, bool focused);
  bool dispatchClick(facebook::react::Tag tag);

  // The path walk and the decision, shared by a real press and by the
  // instrument. Only the translation above them differs.
  bool deliverKeyFrom(RnAppKitView *view, const KeyCombination &pressed);

  AppKitMountingManager *mountingManager_;
  RnAppKitView *surfaceRoot_;
  // The object the root forwards focus changes to. Held so it outlives the
  // root's weak reference to it, exactly as the touch dispatcher does.
  id focusTarget_;
  facebook::react::Tag focusedTag_{0};
};

} // namespace basalt
