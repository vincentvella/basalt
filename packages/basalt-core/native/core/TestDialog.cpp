#include "TestDialog.h"

#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

std::optional<int> scriptedDialogButton() {
  static const std::optional<int> button = []() -> std::optional<int> {
    const char *value = std::getenv("BASALT_TEST_DIALOG");
    if (value == nullptr || *value == '\0') {
      return std::nullopt;
    }
    // A button index, and `dismiss` for the way out -- which is the last
    // button, the one React Native's alerts and this project's share picker
    // both put the cancelling choice in.
    if (std::string(value) == "dismiss") {
      return -1;
    }
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value) {
      return std::nullopt;
    }
    return static_cast<int>(parsed);
  }();
  return button;
}

void presentAlert(const AlertRequest &request, AlertCallback onButton) {
  const std::optional<int> scripted = scriptedDialogButton();
  if (!scripted.has_value()) {
    showAlert(request, std::move(onButton));
    return;
  }

  // The last button for `dismiss`, and the named one otherwise, clamped: a
  // script that names a button an app did not offer should answer the nearest
  // real one rather than an index nothing will understand.
  const int last = request.buttons.empty() ? 0 : static_cast<int>(request.buttons.size()) - 1;
  int button = *scripted < 0 ? last : *scripted;
  if (button > last) {
    button = last;
  }

  // Straight back, on this thread. The platform implementations marshal to the
  // main thread because they have to touch a window; there is no window here,
  // and an answer that arrives before the caller has returned is something
  // every caller already handles -- `AsyncPromise` and the alert module's
  // callback both settle whenever they are told to.
  onButton(button, request.defaultText);
}

std::optional<int> scriptedMenuChoice() {
  static const std::optional<int> choice = []() -> std::optional<int> {
    const char *value = std::getenv("BASALT_TEST_MENU");
    if (value == nullptr || *value == '\0') {
      return std::nullopt;
    }
    if (std::string(value) == "dismiss") {
      return -1;
    }
    char *end = nullptr;
    const long parsed = std::strtol(value, &end, 10);
    if (end == value) {
      return std::nullopt;
    }
    return static_cast<int>(parsed);
  }();
  return choice;
}

void presentMenu(const MenuRequest &request, MenuCallback onChosen) {
  const std::optional<int> scripted = scriptedMenuChoice();
  if (!scripted.has_value()) {
    showMenu(request, std::move(onChosen));
    return;
  }

  // Out of range answers as a dismissal rather than being clamped, which is the
  // opposite of what presentAlert does with a button. A menu's entries are not
  // interchangeable -- picking the nearest one would run something the script
  // did not ask for -- whereas an alert's last button is always the way out.
  //
  // `menuEntryCount` and not `entries.size()`: an index counts every level now,
  // so the valid range is the whole tree and a script naming a submenu's item
  // would otherwise be refused for being past the end of the top level.
  const int count = menuEntryCount(request.entries);
  int index = (*scripted < 0 || *scripted >= count) ? -1 : *scripted;

  // And a dismissal for anything a person could not have chosen either, so that
  // what a script can answer is what a real menu can: a parent opens rather than
  // chooses, a separator is a line, and an entry naming a role this desktop
  // cannot perform is not in the menu at all.
  if (index >= 0) {
    const MenuEntry *entry = menuEntryAt(request.entries, index);
    if (entry == nullptr || entry->isParent() || entry->isSeparator() ||
        !menuEntryShown(*entry)) {
      index = -1;
    }
  }

  // The platform's half of a role still happens: this seam exists to skip the
  // window a person would have clicked, not to skip what choosing the item does.
  // Without this, `copy` under test would resolve a promise and copy nothing.
  //
  // On the UI thread, which this is not. `showContextMenu` is a TurboModule
  // call, so this runs on the JavaScript thread, and `performMenuRole` touches
  // the window and the focused control -- AppKit from the wrong thread does not
  // return. The symptom was the promise never settling at all: `about` opens a
  // panel, and the call to open it never came back.
  if (index >= 0) {
    const MenuEntry *entry = menuEntryAt(request.entries, index);
    if (entry != nullptr && !entry->role.empty()) {
      postToUiThread([role = entry->role] { performMenuRole(role); });
    }
  }
  onChosen(index);
}

std::optional<std::vector<std::string>> scriptedFileDialogPaths() {
  static const std::optional<std::vector<std::string>> paths =
      []() -> std::optional<std::vector<std::string>> {
    const char *value = std::getenv("BASALT_TEST_FILE_DIALOG");
    if (value == nullptr || *value == '\0') {
      return std::nullopt;
    }
    const std::string spec(value);
    if (spec == "cancel") {
      return std::vector<std::string>{};
    }
    // `;` on Windows, where a path starts "C:\\" and splitting on a colon
    // would cut every path in two.
#ifdef _WIN32
    const char separator = ';';
#else
    const char separator = ':';
#endif
    std::vector<std::string> parsed;
    size_t start = 0;
    while (start <= spec.size()) {
      const size_t next = spec.find(separator, start);
      const std::string part =
          spec.substr(start, next == std::string::npos ? std::string::npos : next - start);
      if (!part.empty()) {
        parsed.push_back(part);
      }
      if (next == std::string::npos) {
        break;
      }
      start = next + 1;
    }
    return parsed;
  }();
  return paths;
}

void presentFileDialog(const FileDialogRequest &request, FileDialogCallback onDone) {
  const std::optional<std::vector<std::string>> scripted = scriptedFileDialogPaths();
  if (!scripted.has_value()) {
    showFileDialog(request, std::move(onDone));
    return;
  }

  // An empty list is `cancel`, which is the distinction the whole result shape
  // exists to keep: "nothing was chosen" and "the person pressed Cancel" are
  // not the same answer.
  const bool canceled = scripted->empty();
  std::vector<std::string> paths = *scripted;
  // A save and a folder are one path however many the script named, which is
  // what the platform dialogs enforce and what an app is entitled to assume.
  if (!canceled && request.kind != FileDialogRequest::Kind::OpenFile && paths.size() > 1) {
    paths.resize(1);
  }
  onDone(canceled, paths);
}

} // namespace basalt
