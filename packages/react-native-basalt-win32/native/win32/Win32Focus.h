// Keyboard focus, for everything that is not a <TextInput>.
//
// Deliberately the same shape as react-native-basalt-gtk's GtkFocusManager and
// the AppKit one, down to what makes a view focusable and what activating one
// dispatches, because none of that is about the toolkit: if the three diverge,
// an app's buttons will be reachable from the keyboard on some desktops and not
// others.
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
// ## The chain, all of which is here
//
// The other two desktops have something to start from. GTK hands over its focus
// chain outright; AppKit has a key-view loop that turned out to be unusable for
// a window built without a nib, but at least has a first responder to ask about.
// Here a React Native view is not a window at all -- it is a C++ object painted
// with Direct2D -- so there is nothing for Windows to focus and nothing to ask.
// The order, the current stop, the ring and the key handling are all this file's.
//
// The exception is a <TextInput>, whose peer *is* a real child window. Focus
// there is Win32 focus, moved with SetFocus and reported by EN_SETFOCUS, so a
// field is a stop in the same order as the buttons around it and the two kinds
// of focus have to be kept from being held at once.

#pragma once

#include "RnWin32View.h"
#include "Win32MountingManager.h"

#include <vector>

namespace basalt {

class Win32FocusManager {
 public:
  Win32FocusManager(Win32MountingManager *mountingManager, win32::RnWin32View *surfaceRoot);

  Win32FocusManager(const Win32FocusManager &) = delete;
  Win32FocusManager &operator=(const Win32FocusManager &) = delete;
  Win32FocusManager(Win32FocusManager &&) = delete;
  Win32FocusManager &operator=(Win32FocusManager &&) = delete;

  // The surface root can be replaced after a surface restart; every tag in the
  // old tree goes with it.
  void setSurfaceRoot(win32::RnWin32View *surfaceRoot);

  // Moves focus to the next or previous stop, wrapping, and reports whether
  // anything took it.
  bool moveFocus(bool forward);

  // Activates whatever has focus, as Enter or space does.
  bool activateFocused();

  // WM_KEYDOWN, from the host's window procedure. True when the key was used,
  // which is what keeps Tab from also reaching whatever else might want it.
  bool handleKeyDown(unsigned int virtualKey);

  // An app's own declared shortcuts, asked before the platform's own keys. True
  // when a view from the focused one outwards claimed this combination, in which
  // case it has already been reported; see core/KeyEvents.h.
  bool handleDeclaredKey(unsigned int virtualKey);

  // Called when a <TextInput>'s peer takes Win32 focus, so that a view holding
  // the ring gives it up. The two kinds of focus are different mechanisms and
  // only one of them can be true at a time.
  void textInputTookFocus();

  // The tag of the focused view, or 0. Zero while a <TextInput> holds focus:
  // that is the peer's, and the peer reports its own onFocus.
  facebook::react::Tag focusedTag() const { return focusedTag_; }

 private:
  // Everything Tab should stop on, in tree order. Rebuilt per move rather than
  // cached: views arrive and leave with every mutation, and a stale order is
  // worse than a walk of a tree that is already in memory.
  std::vector<win32::RnWin32View *> collectStops() const;

  void setFocusedView(win32::RnWin32View *view);
  void emitFocus(facebook::react::Tag tag, bool focused);

  Win32MountingManager *mountingManager_;
  win32::RnWin32View *surfaceRoot_;
  facebook::react::Tag focusedTag_{0};
};

} // namespace basalt
