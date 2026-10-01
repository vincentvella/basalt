#include "DevMenu.h"

#include "PlatformServices.h"
#include "TestDialog.h"

#include <react/runtime/ReactHost.h>

#include <folly/dynamic.h>

namespace basalt {

std::vector<DevMenuItem> devMenuItems(facebook::react::ReactHost *reactHost) {
  std::vector<DevMenuItem> items;

  items.push_back(DevMenuItem{
      .label = "Reload",
      .shortcut = "Ctrl+R",
      .action =
          [reactHost] {
            if (reactHost == nullptr) {
              return;
            }
            // Through JavaScript, because the C++ side has no way in: the
            // reload lives on ReactHost's private `reloadReactInstance`, and
            // the only thing wired to it is the callback React Native's own
            // DevSettings module holds. See the header.
            reactHost->emitDeviceEvent(folly::dynamic::array(kDevMenuEvent, kDevMenuReload));
          },
  });

  items.push_back(DevMenuItem{
      .label = "Toggle Element Inspector",
      .shortcut = "Ctrl+I",
      .action =
          [reactHost] {
            if (reactHost == nullptr) {
              return;
            }
            // React Native's own event, handled by AppContainer-dev.js. The
            // inspector it renders is ordinary React views, so it needs
            // nothing of this platform beyond mounting them -- which is why
            // this works on all three hosts without a line of platform code.
            reactHost->emitDeviceEvent(folly::dynamic::array("toggleElementInspector"));
          },
  });

  items.push_back(DevMenuItem{
      .label = "Open Debugger",
      .shortcut = "Ctrl+J",
      .action =
          [reactHost] {
            if (reactHost != nullptr) {
              reactHost->openDebugger();
            }
          },
  });

  return items;
}

void showDevMenu(facebook::react::ReactHost *reactHost, double x, double y) {
  if (reactHost == nullptr) {
    return;
  }

  // Built fresh each time rather than kept: an item's label will eventually
  // depend on state -- "Disable Fast Refresh" versus "Enable" -- and a menu
  // built once would be the first thing to go stale.
  auto items = std::make_shared<std::vector<DevMenuItem>>(devMenuItems(reactHost));

  MenuRequest request;
  request.x = x;
  request.y = y;
  request.entries.reserve(items->size());
  for (const DevMenuItem &item : *items) {
    request.entries.push_back(MenuEntry{
        .label = item.label,
        .enabled = true,
        .shortcut = item.shortcut,
    });
  }

  // Through presentMenu rather than showMenu, so BASALT_TEST_MENU can answer
  // it: a menu nobody dismisses stops an automated run where it stands. See
  // core/TestDialog.h.
  presentMenu(request, [items](int index) {
    // -1 is a dismissal, which is not a failure and is the common case: a menu
    // opened by a keystroke is closed by another one at least as often as it
    // is chosen from.
    if (index < 0 || static_cast<size_t>(index) >= items->size()) {
      return;
    }
    if (const auto &action = (*items)[static_cast<size_t>(index)].action) {
      action();
    }
  });
}

} // namespace basalt
