// An application menu, described once.
//
// The menu bar is the one desktop feature where the three platforms disagree
// about whether it should exist at all, and this file is shaped by that rather
// than pretending otherwise.
//
//   macOS    Every application has one, it lives in the system menu bar, and it
//            is not optional -- it is also load-bearing. AppKit routes a key
//            equivalent through the main menu before anything else sees it, so
//            an app with no Edit menu has no working Cmd-C: the field editor
//            never receives `copy:`. That was true of this host until this file
//            existed, and it is the reason it exists now.
//
//   Windows  Conventional for some applications and absent from many. It is a
//            bar inside the window, so it costs client area.
//
//   GNOME    Discouraged outright: the HIG has said to use a header bar and a
//            menu button since GNOME 3, and GTK4 removed the widget that made
//            the old arrangement easy.
//
// So an app describes a menu here, and each platform decides what to do with
// the description. What it must never do is quietly nothing, which is why
// `applicationMenuSupported()` exists: an app can ask, and this project's own
// documentation can say, rather than an app shipping with a File menu nobody
// on Linux can see.
//
// ## Roles
//
// An item with a `role` is one the platform implements itself -- copy, paste,
// quit, minimise. It never reaches JavaScript, which is not an optimisation: on
// macOS `copy:` has to travel the responder chain to reach whichever field has
// focus, and a round trip through a callback would arrive with the chain gone.
// The role names are Electron's, because that is the vocabulary a desktop
// developer already has.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace basalt {

struct MenuItemModel {
  // What it says. Ignored for a separator, and optional for a role, where the
  // platform's own word for it is better than any this project would choose.
  std::string label;

  // One of the names in kMenuRoles below, or empty for an item the app
  // implements. An unknown role is dropped rather than shown: a menu item that
  // does nothing is worse than one that is not there.
  std::string role;

  // "CmdOrCtrl+O", in Electron's spelling. Ignored for a role, which brings the
  // platform's own shortcut with it.
  std::string accelerator;

  bool enabled{true};
  bool separator{false};

  // What comes back when the app's own item is chosen. Zero for a role or a
  // separator, and for a submenu's parent.
  int id{0};

  std::vector<MenuItemModel> submenu;
};

// The top level of a menu bar: each entry is a menu, and its `submenu` is what
// drops down. An entry with no submenu is dropped -- no desktop has a bare
// command in a menu bar.
using MenuModel = std::vector<MenuItemModel>;

// The roles a platform is expected to implement itself, in Electron's spelling.
// Anything else in `role` is unknown and the item is dropped.
//
// Deliberately short. Each one is a thing every desktop already has a standard
// action and shortcut for; a role that only one platform has would be a role
// apps cannot use.
inline constexpr const char *kMenuRoles[] = {
    "about", "quit",   "undo",  "redo",   "cut",     "copy",
    "paste", "delete", "selectAll", "minimize", "zoom", "close",
    "togglefullscreen",
};

// Whether this platform puts an application menu anywhere a person can see it.
//
// False is not a failure and not a promise to fix: see the header. An app that
// asks can put its commands somewhere else; one that does not is no worse off
// than it was.
bool applicationMenuSupported();

// Installs `menu` as the application menu, replacing whatever was there.
//
// `onChosen` is called with the `id` of an app's own item, on the UI thread. A
// role never arrives there. An empty menu restores the platform's default,
// which on macOS is the minimum an application needs to be closable.
void setApplicationMenu(const MenuModel &menu, std::function<void(int id)> onChosen);

// The menu that is actually installed, as text, for BASALT_DUMP_MENU.
//
// Read back from the platform rather than remembered from the model, which is
// the whole point: it says the description became a real NSMenu or HMENU, with
// the shortcuts the platform attached to its roles. Empty on a platform with no
// menu bar.
//
// One line per item, two spaces of indent per level:
//
//     Edit
//       Copy [cmd+c]
//       Paste [cmd+v]
//       -
//       Nothing doing (disabled)
//
// Called on the UI thread.
std::string describeApplicationMenu();

} // namespace basalt
