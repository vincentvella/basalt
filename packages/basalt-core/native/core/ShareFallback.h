// A share sheet for the desktops that do not have one.
//
// `Share.share()` promises a picker. macOS has `NSSharingServicePicker` and
// means it. Windows has the `DataTransferManager` share UI, reachable through
// WinRT interop. Linux has neither: there is no share portal, GNOME and KDE
// expose no such API, and the nearest thing in the freedesktop specifications
// is `xdg-email`, which is one destination rather than a choice of them.
//
// So the options were to reject on the desktops with no service, or to build
// the smallest honest picker out of what every desktop does have. This is the
// second. A share sheet is a list of places to send something, and the two
// entries on it that every desktop can actually perform are *the clipboard* and
// *an email*. Both already have seams here -- `setClipboardText` and `openUrl`
// -- so this file is a dialog and a `mailto:` and nothing platform-specific at
// all.
//
// It is deliberately not dressed up as a native share sheet. A person sees a
// dialog with the message in it and two things they can do with it, which is
// what is true; pretending to be a service picker that does not exist would be
// worse than being a plain one that does.
//
// An implementation that wants the real thing on Windows should replace its
// `shareContent` and leave this alone; see docs/BACKLOG.md.

#pragma once

#include "PlatformServices.h"

namespace basalt {

// The text a share puts on the clipboard: the message and the URL, whichever of
// them the app supplied, one per line. Exposed for the tests, because it is the
// only part of this with an answer that can be wrong.
std::string shareClipboardText(const ShareRequest &request);

// `mailto:` for the same content, with the title as the subject. Percent-encoded
// as RFC 6068 requires, which matters more than it looks: a message with an
// ampersand in it silently truncates the body of the draft otherwise.
std::string shareMailtoUrl(const ShareRequest &request);

// Shows the picker and reports what was chosen. The implementation of
// `shareContent` on every desktop that has no service of its own.
void shareThroughFallbackPicker(const ShareRequest &request, ShareCallback onDone);

} // namespace basalt
