#include "ActionSheet.h"

#include <algorithm>

namespace basalt {

namespace {

MenuEntry disabledEntry(const std::string &label) {
  // Assigned rather than a designated initializer, for the reason
  // MenuEntry::separator() gives: a partial initializer list is an error under
  // the -Werror the GTK build turns on.
  MenuEntry entry;
  entry.label = label;
  entry.enabled = false;
  return entry;
}

bool contains(const std::vector<int> &values, int value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

} // namespace

ActionSheetMenu actionSheetMenu(const ActionSheetRequest &request) {
  ActionSheetMenu built;
  built.menu.x = request.x;
  built.menu.y = request.y;

  if (!request.title.empty()) {
    built.menu.entries.push_back(disabledEntry(request.title));
  }
  if (!request.message.empty()) {
    built.menu.entries.push_back(disabledEntry(request.message));
  }
  if (!built.menu.entries.empty()) {
    built.menu.entries.push_back(MenuEntry::separator());
  }
  built.offset = static_cast<int>(built.menu.entries.size());

  for (size_t index = 0; index < request.options.size(); ++index) {
    MenuEntry entry;
    entry.label = request.options[index];
    entry.enabled = !contains(request.disabledIndices, static_cast<int>(index));
    built.menu.entries.push_back(entry);
  }

  return built;
}

std::optional<int> actionSheetChoice(const ActionSheetRequest &request,
                                     const ActionSheetMenu &menu,
                                     int chosen) {
  const int count = static_cast<int>(request.options.size());

  if (chosen < 0) {
    // Dismissed. The cancel choice when the app named one, and otherwise
    // nothing at all; see the header.
    if (request.cancelIndex >= 0 && request.cancelIndex < count) {
      return request.cancelIndex;
    }
    return std::nullopt;
  }

  const int option = chosen - menu.offset;
  if (option < 0 || option >= count) {
    // A header, or an index no entry of this menu has. Not reported: the app's
    // callback takes an index into the array it passed, and there is nothing
    // here to pass it.
    return std::nullopt;
  }
  return option;
}

} // namespace basalt
