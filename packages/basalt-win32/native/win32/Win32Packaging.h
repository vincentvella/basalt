// Making this process look like an installed application to Windows.
//
// Unlike macOS, nothing here is about where the executable sits: Windows has no
// bundle. What it has is an **AppUserModelID**, a string the shell uses to
// decide which taskbar button a window belongs to, which jump list it has, and
// -- the reason this file exists -- which application a notification came from.
//
// A desktop application that wants to raise a notification has to do two things,
// and doing only one of them fails silently:
//
//   1. Call `SetCurrentProcessExplicitAppUserModelID` before it shows any UI.
//      Without it the shell derives an id from the process, which changes
//      between a run from a build directory and a run from anywhere else.
//
//   2. Have a Start Menu shortcut whose `System.AppUserModel.ID` property is
//      that same string. This is the part people miss. The shell will not
//      surface a notification for an id it has never seen installed, and a
//      shortcut is how an id becomes installed. Microsoft's own toast
//      documentation says so, and it is why this is in the host rather than in
//      `cli/packageApp.js`: setting that property means `IPropertyStore`, which
//      is COM, and doing it in C++ at startup is simpler and more reliable than
//      doing it in PowerShell from Node.
//
// Both are driven from `core/AppIdentity.h`, which the CLI writes beside the
// bundle. An app with no identity file gets neither, and notifications then
// report that they are unavailable -- which is the answer this host gave before
// any of this existed.
//
// The shortcut is written once and left alone. It is a real file in the
// person's Start Menu, so rewriting it on every run would mean a development
// session churning their menu; `ensureStartMenuShortcut` checks first.

#pragma once

namespace basalt {

struct AppIdentity;

namespace win32 {

// Applies the identity to this process: the AppUserModelID, and a Start Menu
// shortcut carrying it if there is not one already.
//
// Call once, from the host, before any window exists. A no-op for an empty
// identity.
void applyPackaging(const AppIdentity &identity);

// Whether this process has an AppUserModelID, which is what decides whether a
// notification can be raised at all. Read by Win32Notifications.cpp.
bool hasAppUserModelId();

// The identity's id, as applied. Empty when none was.
const wchar_t *appUserModelId();

} // namespace win32
} // namespace basalt
