// The host's half of the Windows menu bar: forwarding WM_COMMAND.
//
// A menu bar's items arrive as WM_COMMAND on the window that owns it, and the
// window procedure is the host's. This is the one line it has to pass on. See
// Win32MenuBar.cpp for what happens to it.

#pragma once

namespace basalt::win32 {

// Handles a WM_COMMAND from the application menu. Returns false when the
// command was not one of its own, so the host can go on treating it as whatever
// else a WM_COMMAND is -- a control's notification, in this host's case.
bool handleMenuCommand(unsigned int command);

} // namespace basalt::win32
