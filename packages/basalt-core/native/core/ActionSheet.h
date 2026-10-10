// `ActionSheetIOS`, which on a desktop is a popup menu.
//
// `ActionSheetManager` is a `get` lookup, so its absence was not a crash: the
// module came back null and `ActionSheetIOS.showActionSheetWithOptions` died on
// `invariant(RCTActionSheetManager, "ActionSheetManager doesn't exist")` at the
// first call. Unlike `Settings`, `Share` and `Alert`, there is no JavaScript to
// replace -- `ActionSheetIOS.js` has no `Platform.OS` branch at all and calls
// the module directly, which is why this is a module and nothing else. Worth
// knowing that the two shapes exist: an iOS-named API is not automatically one
// whose JavaScript refuses to run off iOS.
//
// ## A menu rather than a dialog
//
// An action sheet is a list of choices, one of which usually cancels, shown
// because of something the person just did. On a desktop that is a popup menu,
// and the alternative -- `presentAlert`, which this project already has -- loses
// more than it gains: a dialog can show the sheet's `title` and `message`, and
// it cannot show eight choices without looking like a mistake, cannot grey one
// out, and cannot be dismissed with Escape the way a menu can.
//
// So it is `presentMenu`, and the title and message go in as disabled entries
// at the top. That is a real desktop pattern and it moves the options down the
// list, which is why `offset` below exists: JavaScript numbers the array it
// passed, and a menu index counts every entry.
//
// ## What a desktop cannot do with it
//
// `destructiveButtonIndex` is accepted and ignored. None of the three can style
// one item of a popup menu as dangerous: GTK's `destructive-action` is a button
// class, `NSMenuItem` has no such style, and a Win32 menu item has no colour of
// its own. `tintColor`, `cancelButtonTintColor`, `disabledButtonTintColor` and
// `userInterfaceStyle` are iOS's own and are likewise ignored rather than
// refused, so code written for iOS runs unchanged. `disabledButtonIndices` is
// the one that *does* work, because a menu entry can be greyed.

#pragma once

#include "PlatformServices.h"

#include <optional>
#include <string>
#include <vector>

namespace basalt {

// What the app asked for, read off the options object once.
struct ActionSheetRequest {
  std::string title;
  std::string message;
  std::vector<std::string> options;
  // Indices into `options`, as `disabledButtonIndices` names them.
  std::vector<int> disabledIndices;
  // `cancelButtonIndex`, or -1 when the app named none. It is what a dismissal
  // reports; see actionSheetChoice.
  int cancelIndex{-1};
  // Where the menu goes, in the window's coordinates. Negative means "wherever
  // the pointer is", which is what MenuRequest means by it and what a sheet with
  // no `anchor` wants.
  double x{-1.0};
  double y{-1.0};
};

// The popup menu an action sheet becomes, and how to read an answer back out.
struct ActionSheetMenu {
  MenuRequest menu;
  // How many entries the title, the message and their separator took, which is
  // the difference between a menu index and the option index an app knows.
  int offset{0};
};

ActionSheetMenu actionSheetMenu(const ActionSheetRequest &request);

// Which option to report for the menu index `chosen`, or nothing when the app's
// callback should not be called at all.
//
// Nothing, rather than -1, because that is iOS's behaviour and it is load
// bearing: `UIAlertController` invokes the callback from a button's handler, so
// a sheet that goes away without a button being pressed calls nothing, and an
// app that would otherwise act on "index -1" never has to.
//
// A dismissal is the one case where this platform answers something iOS does
// not. Escape closing a menu is far more ordinary than an iPad popover being
// tapped away, and an app that offered a cancel choice has already said what to
// do with it -- so a dismissal reports `cancelIndex` when there is one, and
// nothing when there is not.
std::optional<int> actionSheetChoice(const ActionSheetRequest &request,
                                    const ActionSheetMenu &menu,
                                    int chosen);

} // namespace basalt
