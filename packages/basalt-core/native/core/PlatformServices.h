// The handful of things a core module needs an operating system for.
//
// One file rather than five seams, because each of these is two or three
// functions and splitting them would mean ten files saying very little. What
// they have in common is that the *module* above each is portable and only the
// last step is not: putting text on a clipboard, handing a URL to whatever
// opens it, and putting a modal dialog on screen.
//
// The fifth seam, after fonts, the text layout manager, the component registry
// and the colour scheme.

#pragma once

#include <cstddef>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

// --- Clipboard ---------------------------------------------------------------

// The clipboard's text, or empty if it holds none.
//
// **Called from the JavaScript thread**, by DesktopClipboardModule, which is
// where `Clipboard.getString()` and `setString()` arrive. This comment used to
// say "on the main thread", which was what the seam wanted rather than what it
// got: the GTK implementation believed it and called GDK from whichever thread
// asked, which deadlocked a CI run on 2026-10-08. See
// PlatformServicesGtk.cpp's clipboard section and backlog/testing.md.
//
// So each implementation answers for its own toolkit's threading rule. GTK hands
// the write to its main thread; Windows posts through a message-only window for
// other work and its clipboard calls are per-thread by design; macOS uses
// NSPasteboard, which is documented as not thread-safe and makes no blocking
// round trip against the main thread. backlog/host-wiring.md carries what is
// left.
std::string clipboardText();
void setClipboardText(const std::string &text);

// --- Opening things ----------------------------------------------------------

// Whether anything is registered to handle this URL. A desktop cannot always
// answer honestly -- both implementations here check the scheme rather than
// asking every installed application -- so this errs towards yes.
bool canOpenUrl(const std::string &url);

// Hands the URL to the desktop. Returns false if it could not be launched.
bool openUrl(const std::string &url);

// --- Alerts ------------------------------------------------------------------

struct AlertRequest {
  std::string title;
  std::string message;
  // In React Native's order, which is also the order the callback reports.
  std::vector<std::string> buttons;
  // A `prompt`, with the text field pre-filled. Empty means a plain alert.
  bool hasTextInput{false};
  std::string defaultText;
  std::string placeholder;
};

// Shows a modal alert and calls `onButton` with the index of the button
// pressed, and the text if the alert had a field.
//
// Must not block: it is called from the JavaScript thread's mount step, and a
// modal run loop there would deadlock the runtime. Implementations marshal to
// the main thread and return at once.
using AlertCallback = std::function<void(int buttonIndex, const std::string &text)>;
void showAlert(const AlertRequest &request, AlertCallback onButton);

// --- File dialogs -------------------------------------------------------------

// One entry in a dialog's file-type filter. `extensions` are without the dot.
struct FileFilter {
  std::string name;
  std::vector<std::string> extensions;
};

struct FileDialogRequest {
  enum class Kind {
    // One or more existing files.
    OpenFile,
    // A path to write to, existing or not. The only kind that asks about
    // overwriting, which every platform does for itself.
    SaveFile,
    // A directory.
    OpenFolder,
  };

  Kind kind{Kind::OpenFile};
  std::string title;
  // Where to start, and for a save, what to call it. A directory for the two
  // open kinds; a file name, optionally with a directory, for a save.
  std::string defaultPath;
  // What the accepting button says. Empty means the system's own word, which
  // is almost always the right one.
  std::string confirmLabel;
  // OpenFile only. Ignored by the other two, because no desktop has a
  // multiple-selection save.
  bool multiple{false};
  std::vector<FileFilter> filters;
};

// Shows a file dialog and calls `onDone` with what was chosen, or with
// `canceled` and nothing.
//
// Must not block, for the same reason `showAlert` must not: this is called from
// the JavaScript thread and a modal run loop there would deadlock the runtime.
// Implementations marshal to the main thread and return at once.
//
// Paths are absolute, and are whatever the platform calls a path -- a Windows
// one has backslashes in it. Nothing here normalises them: an app passes them
// straight back to `fetch`, to a native module, or to the same dialog next
// time, and a "tidied" path is one the system may no longer recognise.
using FileDialogCallback =
    std::function<void(bool canceled, const std::vector<std::string> &paths)>;
void showFileDialog(const FileDialogRequest &request, FileDialogCallback onDone);

// --- Menus --------------------------------------------------------------------

// Whether this desktop can perform `role` in a popup menu.
//
// False for every role a platform has no implementation for: Linux and Windows
// both lack `about`, neither having a platform about panel to ask for. Declared
// here rather than below because MenuEntry asks it.
bool menuRoleSupported(const std::string &role);

// Performs `role` now, on whatever currently has focus. Called on the UI thread,
// immediately before the chosen index is reported.
void performMenuRole(const std::string &role);

// One entry in a popup menu. A separator is an item with an empty label, which
// is how GMenu, NSMenu and an HMENU each spell it too.
struct MenuEntry {
  std::string label;
  // Shown greyed and not choosable. A menu of only disabled items is still a
  // menu, which is what makes this better than leaving the entry out.
  bool enabled{true};
  // Drawn beside the label -- "Cmd+R", "Ctrl+R". Decoration only: nothing here
  // binds a key, because the shortcut that opened the menu and the shortcuts
  // inside it are answered in different places on all three desktops.
  std::string shortcut;

  // One of kMenuRoles in MenuModel.h, or empty for an item the app implements
  // itself. A role is performed by the platform -- the Copy that actually
  // copies -- and still reports its index, so a caller's `onSelect` runs too.
  //
  // A role this platform cannot perform is dropped rather than shown, the same
  // policy the menu bar has: an item that does nothing is worse than one that is
  // not there. `menuRoleSupported` says which, and the three do not agree.
  std::string role;

  // Nested entries. A parent is not choosable itself: it opens its children.
  //
  // Both a submenu and a role on one entry is a caller's mistake; the submenu
  // wins, because that is the one with something under it to lose.
  std::vector<MenuEntry> submenu;

  // Whether the entry carries a mark, and which kind.
  //
  // Two fields rather than one `checked` bool, and GTK is the reason. A GMenu
  // item has no checked attribute at all: the mark comes from the shape of the
  // GAction behind it, and GtkPopoverMenu picks a tick or a circle from whether
  // that action is boolean-stateful or string-stateful with a target. So a bool
  // cannot say which to draw, and the kind has to be its own field.
  //
  // `Radio` is a drawing hint rather than a capability. Windows draws a bullet
  // and GTK a circle; NSMenu has no radio item, and the mark macOS puts beside a
  // selected member of an exclusive group is a tick, which is what Apple's own
  // guidance prescribes. All three can mark a chosen item, so neither kind is
  // refused anywhere.
  //
  // Nothing tri-state: NSControlStateValueMixed has no counterpart on the other
  // two, so offering it would be offering something only one desktop has.
  enum class Mark { None, Check, Radio };
  Mark mark{Mark::None};
  bool checked{false};

  static MenuEntry separator() {
    // Assigned rather than a designated initializer. One that names some fields
    // and not others is a -Wmissing-field-initializers error under -Werror, which
    // the GTK build turns on and the AppKit one does not -- so adding `role` and
    // `submenu` compiled on a Mac and failed all three Linux shards. Written this
    // way, the next field costs nothing.
    MenuEntry entry;
    entry.enabled = false;
    return entry;
  }

  bool isSeparator() const {
    return label.empty() && submenu.empty();
  }

  bool isParent() const {
    return !submenu.empty();
  }
};

struct MenuRequest {
  std::vector<MenuEntry> entries;
  // Where to put it, in the window's coordinates. A negative point means "wherever
  // the pointer is", which is what a menu opened from the keyboard wants.
  double x{-1.0};
  double y{-1.0};
};

// --- Indexing a menu that is now a tree ---------------------------------------
//
// `showMenu` answers with an index, and did so when a popup was a flat list. The
// index is an entry's position in a *pre-order* walk of the whole menu, counting
// separators and submenu parents -- so for a flat list it is still the position
// in the vector, and every caller that predates nesting means what it always
// meant.
//
// Hosts use `walkMenuEntries` to tag their items and `menuEntryAt` to find what
// an index named, which is how a role is performed and how a parent is told from
// a leaf.
namespace detail {

template <typename Visitor>
int walkMenuEntriesFrom(const std::vector<MenuEntry> &entries, int next, Visitor &visit) {
  for (const MenuEntry &entry : entries) {
    const int index = next++;
    visit(entry, index);
    if (entry.isParent()) {
      next = walkMenuEntriesFrom(entry.submenu, next, visit);
    }
  }
  return next;
}

} // namespace detail

template <typename Visitor>
void walkMenuEntries(const std::vector<MenuEntry> &entries, Visitor &&visit) {
  detail::walkMenuEntriesFrom(entries, 0, visit);
}

// The run of consecutive radio entries `index` belongs to, as a half-open range
// of pre-order indexes, or {index, index} when it is not a radio entry.
//
// A group is a maximal run of adjacent radio siblings, ended by a separator, by a
// non-radio entry, or by the end of the level. That is a positional definition
// rather than an identifier on each item, and it fits all three: Win32's
// CheckMenuRadioItem wants a contiguous range, GTK wants one action per run, and
// AppKit wants nothing. It also cannot straddle a GMenu section, since the GTK
// builder breaks into a new section at every separator.
//
// The cost is that two adjacent groups with no separator between them read as
// one. A separator is what every desktop draws between two groups anyway.
inline std::pair<int, int> menuRadioRun(const std::vector<MenuEntry> &siblings,
                                        int firstIndexOfLevel,
                                        std::size_t at) {
  const int index = firstIndexOfLevel + static_cast<int>(at);
  if (at >= siblings.size() || siblings[at].mark != MenuEntry::Mark::Radio) {
    return {index, index};
  }
  std::size_t begin = at;
  while (begin > 0 && siblings[begin - 1].mark == MenuEntry::Mark::Radio) {
    begin--;
  }
  std::size_t end = at + 1;
  while (end < siblings.size() && siblings[end].mark == MenuEntry::Mark::Radio) {
    end++;
  }
  return {firstIndexOfLevel + static_cast<int>(begin),
          firstIndexOfLevel + static_cast<int>(end)};
}

// How many entries a menu has, counting every level. The number of valid
// indexes, which is not `entries.size()` once anything nests.
inline int menuEntryCount(const std::vector<MenuEntry> &entries) {
  int count = 0;
  walkMenuEntries(entries, [&](const MenuEntry &, int) { ++count; });
  return count;
}

inline const MenuEntry *menuEntryAt(const std::vector<MenuEntry> &entries, int index) {
  const MenuEntry *found = nullptr;
  walkMenuEntries(entries, [&](const MenuEntry &entry, int at) {
    if (at == index) {
      found = &entry;
    }
  });
  return found;
}

// Whether an entry is drawn at all.
//
// False for a role this desktop cannot perform, and for a parent with nothing
// under it that would be drawn. A dropped entry is still *counted*, which is the
// part that matters: JavaScript numbers the list it passed, so an entry
// disappearing from the count would shift every index after it onto the wrong
// handler. See walkMenuEntries.
inline bool menuEntryShown(const MenuEntry &entry);

inline bool anyMenuEntryShown(const std::vector<MenuEntry> &entries) {
  for (const MenuEntry &entry : entries) {
    if (menuEntryShown(entry)) {
      return true;
    }
  }
  return false;
}

inline bool menuEntryShown(const MenuEntry &entry) {
  if (!entry.role.empty() && !menuRoleSupported(entry.role)) {
    return false;
  }
  if (entry.isParent()) {
    // An empty submenu is a dead end rather than an item, which is what a parent
    // becomes when every child named a role this desktop does not have.
    return anyMenuEntryShown(entry.submenu);
  }
  return true;
}

// Shows a popup menu and calls `onChosen` with the index of the entry picked,
// or -1 when it was dismissed. Indexes count separators, so they line up with
// the vector that was passed in.
//
// Must not block, for the same reason `showAlert` must not: this can be called
// from the JavaScript thread, and a menu's own run loop there would deadlock
// the runtime. Implementations marshal to the main thread and return at once.
using MenuCallback = std::function<void(int index)>;
void showMenu(const MenuRequest &request, MenuCallback onChosen);

// --- Sharing ------------------------------------------------------------------

// What an app asked to share. React Native's `Share.share()` takes `message`,
// `url` and `title`, and marks each as belonging to one platform or the other;
// a desktop has no reason to honour that split, so all three arrive and each
// implementation uses what it can.
struct ShareRequest {
  std::string message;
  std::string url;
  std::string title;
  // `options.dialogTitle`, which names the picker on the platforms that show
  // one they did not draw themselves.
  std::string dialogTitle;
};

// What became of it. React Native's promise resolves with `sharedAction` or
// `dismissedAction` and rejects on anything else, so these are the three
// answers it can carry and no more.
enum class ShareOutcome { Shared, Dismissed, Failed };

// `message` is meaningful only for Failed, and becomes the rejection's reason.
using ShareCallback = std::function<void(ShareOutcome outcome, const std::string &message)>;

// Puts the content in front of the user and reports what they did with it.
//
// Must not block, for the same reason `showAlert` must not: it is called from
// the JavaScript thread and a modal run loop there would deadlock the runtime.
// Implementations marshal to the main thread and return at once, and the
// callback comes later -- possibly much later, because a share sheet is on
// screen until a person does something with it.
//
// Only macOS has a share service of its own. The other two go through
// core/ShareFallback.h, which builds a picker from a clipboard and a mail
// client; see its header for why that is the honest answer rather than a
// rejection.
void shareContent(const ShareRequest &request, ShareCallback onDone);

// Runs `work` on the UI thread after `milliseconds`, once.
//
// Every gesture recogniser needs this and nothing else does yet: a long press
// activates because nothing happened for half a second, and a double tap fails
// because a second tap did not arrive. Both are "wake me if this is still true
// later", which cannot be expressed by reacting to input alone.
//
// The callback runs on the thread that draws -- the same one input arrives on
// -- so what it touches needs no lock. There is no way to cancel: a recogniser
// that has moved on checks its own state when it wakes, which is simpler than
// owning a handle and correct even when the work has already started.
void postDelayed(double milliseconds, std::function<void()> work);

// Runs `work` on the UI thread, soon, in the order posted.
//
// The gesture registry is UI-thread state -- input arrives there and the
// recognisers run there -- but the module that configures it is called from
// JavaScript. Rather than lock the registry, its callers hop threads, which is
// the same rule the mounting managers keep and assert.
void postToUiThread(std::function<void()> work);

// Whether this is the thread that draws.
//
// Fabric dispatches an event from whichever thread produced it -- a touch from
// the UI thread, a layout event from the JavaScript thread -- and anything that
// answers by touching a worklet runtime has to know which it is on. That is the
// one caller today; see core/ReanimatedModule.cpp.
bool isUiThread();

} // namespace basalt
