// Answering a modal dialog without showing one.
//
// The same escape hatch `BASALT_TEST_TAP` is, for the same reason and with the
// same limits. A modal dialog is the one piece of interface an automated run
// cannot get past: there is nobody to press a button, the window system will
// not synthesise a press into a sheet, and on macOS a sheet that is up stops
// `[NSApp terminate:]` outright -- so a run that shows one hangs until it is
// killed.
//
// ## Why not drive the real dialog
//
// Because that is where the flakiness lives, and the platforms say so
// themselves. Apple's own mechanism for this in UI tests,
// `addUIInterruptionMonitor`, is documented as not firing on recent iOS
// versions, and the workaround people reach for -- pressing the button through
// SpringBoard -- is widely reported as flaky; Apple's own advice for permission
// dialogs is to pre-grant them through launch arguments rather than to press
// anything. Detox, which is the closest thing React Native has to a house
// end-to-end runner, states the principle outright: gray box rather than black
// box, monitor the app from the inside, because that is what fights flakiness
// at the core.
//
// Driving it from outside is also not available here. AT-SPI on Linux, the
// Accessibility API on macOS and UI Automation on Windows can each press a
// button in a dialog, and each needs a permission or a session an automated run
// does not have -- which is the same wall `BASALT_TEST_TAP` exists because of.
//
// ## What this skips, exactly
//
// The dialog's presentation and nothing else. The request is built by the same
// module from the same JavaScript, the answer travels back through the same
// callback on the same thread, and every line above the platform runs. What
// goes untested is that the toolkit can put a dialog on screen and get a press
// out of it, which is the same thing every other instrument in this project
// leaves to a person.

#pragma once

#include "PlatformServices.h"

#include <vector>

#include <optional>

namespace basalt {

// The button index BASALT_TEST_DIALOG names, or nothing when it is unset.
//
// Read once and cached: an environment variable cannot change under a running
// process, and this is consulted on a path that runs whenever an app asks a
// question.
std::optional<int> scriptedDialogButton();

// `showAlert`, or the scripted answer when there is one.
//
// Everything that would put a modal dialog on screen goes through here rather
// than calling the platform directly, so the instrument covers the share
// picker's fallback as well as `Alert.alert` -- and covers them by *running*
// them: a scripted "Copy" still copies, because only the presentation is
// skipped.
//
// `dismiss` answers with the last button, which is where React Native's alerts
// and this project's share picker both put the cancelling choice.
void presentAlert(const AlertRequest &request, AlertCallback onButton);

// Which entry BASALT_TEST_MENU names, or nothing when it is unset. `dismiss`
// is -1, which is what closing a menu without choosing reports.
//
// A popup menu is the second thing an automated run cannot get past, and for a
// sharper reason than a dialog: on macOS `popUpMenuPositioningItem` runs the
// menu's own tracking loop on the main thread, so a menu nobody dismisses
// stops the process where it stands.
std::optional<int> scriptedMenuChoice();

// `showMenu`, or the scripted answer when there is one. Everything that would
// put a popup menu on screen goes through here rather than calling the platform
// directly, for the same reason `presentAlert` exists.
void presentMenu(const MenuRequest &request, MenuCallback onChosen);

// What BASALT_TEST_FILE_DIALOG names, or nothing when it is unset.
//
// `cancel` is the person pressing Cancel. Anything else is a list of paths
// separated by the platform's path separator -- `:` everywhere but Windows,
// where it is `;`, because a Windows path has a colon two characters in.
//
// A file dialog is the third thing an automated run cannot get past, and the
// one with the most to prove afterwards: what an app does with a path is the
// interesting part, and it cannot be reached at all without one.
std::optional<std::vector<std::string>> scriptedFileDialogPaths();

// `showFileDialog`, or the scripted answer when there is one. Everything that
// would put a file dialog on screen goes through here, for the same reason
// `presentAlert` exists.
void presentFileDialog(const FileDialogRequest &request, FileDialogCallback onDone);

} // namespace basalt
