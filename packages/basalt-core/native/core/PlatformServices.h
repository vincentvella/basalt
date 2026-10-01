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

#include <functional>
#include <string>
#include <vector>

namespace basalt {

// --- Clipboard ---------------------------------------------------------------

// The clipboard's text, or empty if it holds none. Called on the main thread.
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

  static MenuEntry separator() {
    return MenuEntry{.label = {}, .enabled = false, .shortcut = {}};
  }

  bool isSeparator() const {
    return label.empty();
  }
};

struct MenuRequest {
  std::vector<MenuEntry> entries;
  // Where to put it, in the window's coordinates. A negative point means "wherever
  // the pointer is", which is what a menu opened from the keyboard wants.
  double x{-1.0};
  double y{-1.0};
};

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
