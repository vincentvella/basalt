// Which GtkWindow a surface id means.
//
// The same seam AppKitAppWindow.h describes, written for this toolkit and for
// the same reason: the registry of open windows is `main_gtk.cpp`'s, and
// everything that resolves one is compiled into the mounting library, which
// the host links rather than the other way round. So the host installs a
// lookup at startup and this is what the window module asks.
//
// Not installed is the normal case for a test binary and for the mount
// harness: neither opens a second window, and neither should grow a registry
// to keep compiling. `gtkAppWindowFor` falls back to the toplevel scan, which
// is what every caller here did before windows had ids.

#pragma once

#include <react/renderer/core/ReactPrimitives.h>

#include <functional>

typedef struct _GtkWindow GtkWindow;

namespace basalt {

// The app's own window, by the rule this host has always used: the active
// toplevel, falling back to the first visible one. UI thread only.
GtkWindow *gtkAppWindow();

using GtkAppWindowLookup = std::function<GtkWindow *(facebook::react::SurfaceId)>;
void setGtkAppWindowLookup(GtkAppWindowLookup lookup);

// The window with this surface id, or the app's own for `kAppWindow` and for
// an id the lookup does not know. UI thread only.
GtkWindow *gtkAppWindowFor(facebook::react::SurfaceId window);

} // namespace basalt
