// The Linux half of core/MenuModel.h: no application menu, and why.
//
// This is not unimplemented. GNOME's guidelines have said since GNOME 3 that an
// application should use a header bar with a menu button rather than a menu
// bar, GTK4 removed `GtkMenuBar` along with the rest of GtkMenu, and this
// host's window already has a header bar -- which is where a GTK app's menu
// belongs and which `<TitleBar>` already lets an app fill.
//
// Answering `applicationMenuSupported() == false` is the honest reply, and it
// is the reason that function exists: an app can ask, put its commands in its
// own header, and ship something a GNOME user recognises, rather than shipping
// a File menu nobody on Linux can see.
//
// What Linux does *not* lack is menus. `showMenu` in PlatformServicesGtk.cpp is
// a real GtkPopoverMenu, and it is what a header-bar menu button opens; the
// developer menu already uses it. What is missing is only the bar.
//
// If this ever changes, the shape to build is `GtkPopoverMenuBar` over a
// `GMenuModel`, packed above the surface root -- which is a layout change to
// the window rather than an addition to this file, because the surface would
// no longer start at the top of the content area.

#include "MenuModel.h"

#include <string>

namespace basalt {

bool applicationMenuSupported() {
  return false;
}

std::string describeApplicationMenu() {
  // Nothing is installed, so there is nothing to describe. The empty string is
  // what a test asserting "this platform has no menu bar" reads.
  return {};
}

void setApplicationMenu(const MenuModel & /*menu*/, std::function<void(int)> /*onChosen*/) {
  // Deliberately nothing. An app that checked `applicationMenuSupported()`
  // already knows; one that did not is no worse off than before it described a
  // menu, and a warning on every render would be noise it cannot act on.
}

} // namespace basalt
