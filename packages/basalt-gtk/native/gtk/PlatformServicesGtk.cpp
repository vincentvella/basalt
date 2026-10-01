// Linux's half of the platform-services seam: clipboard, opening a URI, alerts.

#include "PlatformServices.h"
#include "ShareFallback.h"

#include <gtk/gtk.h>

#include <cstdlib>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

namespace {

GdkClipboard *defaultClipboard() {
  GdkDisplay *display = gdk_display_get_default();
  return display != nullptr ? gdk_display_get_clipboard(display) : nullptr;
}

} // namespace

// --- Clipboard ---------------------------------------------------------------

// GdkClipboard reads asynchronously -- it may have to ask another process for
// the text -- and this seam is synchronous because `Clipboard.getString()`
// returns a promise that core resolves at once.
//
// The compromise is the content provider's *local* value, which is what this
// application last put there, plus nothing. Reading another application's
// clipboard needs the async read and a promise core does not yet hand down.
// Saying so here beats an empty string that looks like an empty clipboard.
std::string clipboardText() {
  GdkClipboard *clipboard = defaultClipboard();
  if (clipboard == nullptr) {
    return {};
  }

  GdkContentProvider *provider = gdk_clipboard_get_content(clipboard);
  if (provider == nullptr) {
    // Owned by another application. See the note above.
    return {};
  }

  GValue value = G_VALUE_INIT;
  g_value_init(&value, G_TYPE_STRING);
  std::string text;
  if (gdk_content_provider_get_value(provider, &value, nullptr)) {
    const char *string = g_value_get_string(&value);
    if (string != nullptr) {
      text = string;
    }
  }
  g_value_unset(&value);
  return text;
}

void setClipboardText(const std::string &text) {
  GdkClipboard *clipboard = defaultClipboard();
  if (clipboard == nullptr) {
    return;
  }
  gdk_clipboard_set_text(clipboard, text.c_str());
}

// --- Opening things ----------------------------------------------------------

bool canOpenUrl(const std::string &url) {
  if (url.empty()) {
    return false;
  }
  char *scheme = g_uri_parse_scheme(url.c_str());
  if (scheme == nullptr) {
    return false;
  }
  // Whether anything is registered to handle the scheme. The desktop's own
  // answer, from the same database that would do the opening.
  GAppInfo *handler = g_app_info_get_default_for_uri_scheme(scheme);
  g_free(scheme);
  if (handler == nullptr) {
    return false;
  }
  g_object_unref(handler);
  return true;
}

bool openUrl(const std::string &url) {
  GError *error = nullptr;
  const gboolean launched =
      g_app_info_launch_default_for_uri(url.c_str(), nullptr, &error);
  if (launched == FALSE) {
    if (error != nullptr) {
      g_warning("could not open %s: %s", url.c_str(), error->message);
      g_clear_error(&error);
    }
    return false;
  }
  return true;
}

// --- Sharing -----------------------------------------------------------------

// No share service on this desktop, and no portal for one either: the
// freedesktop specifications have `xdg-email` and nothing that offers a choice
// of destinations. So this is the picker core/ShareFallback.h builds out of a
// clipboard and a mail client, which is what every desktop does have.
//
// Nothing here beyond the call, because there is nothing platform-specific
// left in it -- it goes through showAlert, setClipboardText and openUrl, all of
// which are implemented above.
void shareContent(const ShareRequest &request, ShareCallback onDone) {
  shareThroughFallbackPicker(request, std::move(onDone));
}

// --- Alerts ------------------------------------------------------------------

namespace {

struct PendingAlert {
  AlertRequest request;
  AlertCallback onButton;
  GtkWidget *field{nullptr};
};

void onAlertResponse(GObject *source, GAsyncResult *result, gpointer userData) {
  std::unique_ptr<PendingAlert> pending{static_cast<PendingAlert *>(userData)};

  GError *error = nullptr;
  const int index =
      gtk_alert_dialog_choose_finish(GTK_ALERT_DIALOG(source), result, &error);
  if (error != nullptr) {
    // Dismissed without choosing -- Escape, or the window closed. React Native
    // has no "cancelled" here, and reporting the last button would run whatever
    // it does; index 0 is the primary, which is what a plain alert's only
    // button is.
    g_clear_error(&error);
    pending->onButton(0, {});
    return;
  }
  pending->onButton(index, {});
}

gboolean showAlertOnMainThread(gpointer userData) {
  auto *pending = static_cast<PendingAlert *>(userData);

  GtkAlertDialog *dialog = gtk_alert_dialog_new("%s", pending->request.title.c_str());
  if (!pending->request.message.empty()) {
    gtk_alert_dialog_set_detail(dialog, pending->request.message.c_str());
  }

  std::vector<const char *> labels;
  labels.reserve(pending->request.buttons.size() + 1);
  for (const auto &label : pending->request.buttons) {
    labels.push_back(label.c_str());
  }
  labels.push_back(nullptr);
  gtk_alert_dialog_set_buttons(dialog, labels.data());
  // The first is React Native's primary, and the same one onAlertResponse
  // reports when the dialog is dismissed without a choice.
  gtk_alert_dialog_set_default_button(dialog, 0);
  gtk_alert_dialog_set_cancel_button(dialog, 0);

  gtk_alert_dialog_choose(dialog, nullptr, nullptr, onAlertResponse, pending);
  g_object_unref(dialog);
  return G_SOURCE_REMOVE;
}

} // namespace

void showAlert(const AlertRequest &request, AlertCallback onButton) {
  if (request.hasTextInput) {
    // GtkAlertDialog has no text field, and building one means a GtkDialog with
    // its own layout. Not done: a prompt that silently became a plain alert
    // would lose whatever the user was meant to type.
    g_warning("Alert.prompt is not implemented on this platform");
  }

  auto *pending = new PendingAlert{request, std::move(onButton), nullptr};
  // Onto the GTK main thread. This is called from the JavaScript thread, and
  // a dialog may only be created there.
  g_idle_add_full(G_PRIORITY_DEFAULT, showAlertOnMainThread, pending, nullptr);
}

// --- File dialogs -------------------------------------------------------------

namespace {

// The window a popup belongs to. GTK has no "the app's window": what it has is
// a list of toplevels, so this takes the active one, or the first visible one
// when nothing is focused -- which is what an automated run without a
// compositor looks like.
GtkWindow *activeWindow() {
  GListModel *toplevels = gtk_window_get_toplevels();
  if (toplevels == nullptr) {
    return nullptr;
  }
  GtkWindow *fallback = nullptr;
  const guint count = g_list_model_get_n_items(toplevels);
  for (guint i = 0; i < count; i++) {
    auto *window = static_cast<GtkWindow *>(g_list_model_get_item(toplevels, i));
    if (window == nullptr) {
      continue;
    }
    const bool visible = gtk_widget_get_visible(GTK_WIDGET(window)) != FALSE;
    if (visible && gtk_window_is_active(window)) {
      g_object_unref(window);
      return window;
    }
    if (visible && fallback == nullptr) {
      fallback = window;
    }
    g_object_unref(window);
  }
  return fallback;
}

struct PendingFileDialog {
  FileDialogRequest request;
  FileDialogCallback onDone;
};

// The filters, as a GListModel of GtkFileFilter. GTK takes them that way and
// not as a list of patterns, which is why this is not two lines.
GListModel *buildFilters(const std::vector<FileFilter> &filters) {
  if (filters.empty()) {
    return nullptr;
  }
  GListStore *store = g_list_store_new(GTK_TYPE_FILE_FILTER);
  for (const FileFilter &filter : filters) {
    GtkFileFilter *entry = gtk_file_filter_new();
    gtk_file_filter_set_name(entry, filter.name.empty() ? nullptr : filter.name.c_str());
    for (const std::string &extension : filter.extensions) {
      // A glob rather than a MIME type: an extension is what the caller gave,
      // and guessing a type from it would be a second guess on top of theirs.
      const std::string pattern = "*." + extension;
      gtk_file_filter_add_pattern(entry, pattern.c_str());
    }
    g_list_store_append(store, entry);
    g_object_unref(entry);
  }
  return G_LIST_MODEL(store);
}

// One answer, whichever call produced it. GTK's three finishers return a GFile
// or a GListModel of them, and both arrive here as paths.
void answerWith(PendingFileDialog *pending, GFile *file, GListModel *files, GError *error) {
  std::vector<std::string> paths;
  if (file != nullptr) {
    char *path = g_file_get_path(file);
    if (path != nullptr) {
      paths.emplace_back(path);
      g_free(path);
    }
  }
  if (files != nullptr) {
    const guint count = g_list_model_get_n_items(files);
    for (guint i = 0; i < count; i++) {
      auto *item = static_cast<GFile *>(g_list_model_get_item(files, i));
      if (item == nullptr) {
        continue;
      }
      char *path = g_file_get_path(item);
      if (path != nullptr) {
        paths.emplace_back(path);
        g_free(path);
      }
      g_object_unref(item);
    }
  }

  // An error here is a dismissal: GTK reports Cancel as
  // GTK_DIALOG_ERROR_DISMISSED rather than as an empty answer. A real failure
  // is reported the same way on purpose -- an app cannot do anything different
  // with "the portal is not running" than with "the person said no", and
  // rejecting the promise would make every caller write a catch for it.
  const bool canceled = error != nullptr || paths.empty();
  pending->onDone(canceled, paths);
}

void onOpenFinished(GObject *source, GAsyncResult *result, gpointer userData) {
  std::unique_ptr<PendingFileDialog> pending{static_cast<PendingFileDialog *>(userData)};
  GError *error = nullptr;
  GFile *file = gtk_file_dialog_open_finish(GTK_FILE_DIALOG(source), result, &error);
  answerWith(pending.get(), file, nullptr, error);
  g_clear_object(&file);
  g_clear_error(&error);
}

void onOpenMultipleFinished(GObject *source, GAsyncResult *result, gpointer userData) {
  std::unique_ptr<PendingFileDialog> pending{static_cast<PendingFileDialog *>(userData)};
  GError *error = nullptr;
  GListModel *files = gtk_file_dialog_open_multiple_finish(GTK_FILE_DIALOG(source), result, &error);
  answerWith(pending.get(), nullptr, files, error);
  g_clear_object(&files);
  g_clear_error(&error);
}

void onSaveFinished(GObject *source, GAsyncResult *result, gpointer userData) {
  std::unique_ptr<PendingFileDialog> pending{static_cast<PendingFileDialog *>(userData)};
  GError *error = nullptr;
  GFile *file = gtk_file_dialog_save_finish(GTK_FILE_DIALOG(source), result, &error);
  answerWith(pending.get(), file, nullptr, error);
  g_clear_object(&file);
  g_clear_error(&error);
}

void onFolderFinished(GObject *source, GAsyncResult *result, gpointer userData) {
  std::unique_ptr<PendingFileDialog> pending{static_cast<PendingFileDialog *>(userData)};
  GError *error = nullptr;
  GFile *folder = gtk_file_dialog_select_folder_finish(GTK_FILE_DIALOG(source), result, &error);
  answerWith(pending.get(), folder, nullptr, error);
  g_clear_object(&folder);
  g_clear_error(&error);
}

gboolean showFileDialogOnMainThread(gpointer userData) {
  auto *pending = static_cast<PendingFileDialog *>(userData);
  const FileDialogRequest &request = pending->request;

  GtkFileDialog *dialog = gtk_file_dialog_new();
  if (!request.title.empty()) {
    gtk_file_dialog_set_title(dialog, request.title.c_str());
  }
  if (!request.confirmLabel.empty()) {
    gtk_file_dialog_set_accept_label(dialog, request.confirmLabel.c_str());
  }
  if (GListModel *filters = buildFilters(request.filters)) {
    gtk_file_dialog_set_filters(dialog, filters);
    g_object_unref(filters);
  }
  if (!request.defaultPath.empty()) {
    if (request.kind == FileDialogRequest::Kind::SaveFile) {
      // A save takes a name and a folder separately, and the caller may have
      // given either or both in one string.
      GFile *file = g_file_new_for_path(request.defaultPath.c_str());
      char *name = g_file_get_basename(file);
      GFile *folder = g_file_get_parent(file);
      if (name != nullptr) {
        gtk_file_dialog_set_initial_name(dialog, name);
      }
      if (folder != nullptr) {
        gtk_file_dialog_set_initial_folder(dialog, folder);
      }
      g_free(name);
      g_clear_object(&folder);
      g_object_unref(file);
    } else {
      GFile *folder = g_file_new_for_path(request.defaultPath.c_str());
      gtk_file_dialog_set_initial_folder(dialog, folder);
      g_object_unref(folder);
    }
  }

  GtkWindow *parent = activeWindow();
  switch (request.kind) {
    case FileDialogRequest::Kind::OpenFile:
      if (request.multiple) {
        gtk_file_dialog_open_multiple(dialog, parent, nullptr, onOpenMultipleFinished, pending);
      } else {
        gtk_file_dialog_open(dialog, parent, nullptr, onOpenFinished, pending);
      }
      break;
    case FileDialogRequest::Kind::SaveFile:
      gtk_file_dialog_save(dialog, parent, nullptr, onSaveFinished, pending);
      break;
    case FileDialogRequest::Kind::OpenFolder:
      gtk_file_dialog_select_folder(dialog, parent, nullptr, onFolderFinished, pending);
      break;
  }
  g_object_unref(dialog);
  return G_SOURCE_REMOVE;
}

} // namespace

void showFileDialog(const FileDialogRequest &request, FileDialogCallback onDone) {
  auto *pending = new PendingFileDialog{request, std::move(onDone)};
  // Onto the GTK main thread, for the same reason showAlert is: this is called
  // from the JavaScript thread, and a dialog may only be made here.
  g_idle_add_full(G_PRIORITY_DEFAULT, showFileDialogOnMainThread, pending, nullptr);
}

// --- Menus -------------------------------------------------------------------

namespace {

struct PendingMenu {
  MenuRequest request;
  MenuCallback onChosen;
  GtkWidget *popover{nullptr};
  bool answered{false};
};

// "Ctrl+R" into the "<Control>r" GTK wants for a displayed accelerator.
// Display only: nothing here binds the key. An unparseable string becomes
// empty, which shows no accelerator rather than a wrong one.
std::string toGtkAccelerator(const std::string &shortcut) {
  if (shortcut.empty()) {
    return {};
  }
  std::string out;
  size_t start = 0;
  while (true) {
    const size_t plus = shortcut.find('+', start);
    const std::string part = shortcut.substr(
        start, plus == std::string::npos ? std::string::npos : plus - start);
    if (plus == std::string::npos) {
      // The key itself, lowercased: GTK matches keyval names, and "R" is not one.
      for (char c : part) {
        out += static_cast<char>(g_ascii_tolower(c));
      }
      break;
    }
    if (part == "Ctrl" || part == "Control") {
      out += "<Control>";
    } else if (part == "Shift") {
      out += "<Shift>";
    } else if (part == "Alt" || part == "Option") {
      out += "<Alt>";
    } else if (part == "Cmd" || part == "Meta" || part == "Super") {
      out += "<Meta>";
    } else {
      return {};
    }
    start = plus + 1;
  }
  return out;
}

// Answers once, whichever comes first: an item activating, or the popover
// closing with nothing chosen. Both happen for a menu that was used, and the
// order is not guaranteed.
void finishMenu(PendingMenu *pending, int index) {
  if (pending->answered) {
    return;
  }
  pending->answered = true;
  pending->onChosen(index);
}

void onMenuItemActivated(GSimpleAction *action, GVariant * /*parameter*/, gpointer userData) {
  auto *pending = static_cast<PendingMenu *>(userData);
  // The action is named "item<N>", which is where the index comes from: a
  // GMenu carries no index of its own, and the position in the model is not
  // the position in the vector once separators are sections.
  const char *name = g_action_get_name(G_ACTION(action));
  finishMenu(pending, name != nullptr ? std::atoi(name + 4) : -1);
  if (pending->popover != nullptr) {
    gtk_popover_popdown(GTK_POPOVER(pending->popover));
  }
}

void onMenuClosed(GtkPopover * /*popover*/, gpointer userData) {
  auto *pending = static_cast<PendingMenu *>(userData);
  finishMenu(pending, -1);
  // Unparented from an idle rather than here: GTK is still inside the
  // popover's own signal emission, and destroying the widget it is emitting
  // from is how a popup menu turns into a use-after-free.
  g_idle_add_full(
      G_PRIORITY_DEFAULT_IDLE,
      [](gpointer data) -> gboolean {
        std::unique_ptr<PendingMenu> owned{static_cast<PendingMenu *>(data)};
        if (owned->popover != nullptr) {
          gtk_widget_unparent(owned->popover);
        }
        return G_SOURCE_REMOVE;
      },
      pending,
      nullptr);
}

gboolean showMenuOnMainThread(gpointer userData) {
  auto *pending = static_cast<PendingMenu *>(userData);

  GtkWindow *window = activeWindow();
  GtkWidget *anchor = window != nullptr ? gtk_window_get_child(window) : nullptr;
  if (anchor == nullptr) {
    finishMenu(pending, -1);
    delete pending;
    return G_SOURCE_REMOVE;
  }

  // Separators are sections rather than entries: a GMenu has no separator item,
  // and two sections are drawn with a line between them. The indexes the
  // caller gets back still count them, which is what the action names carry.
  GMenu *model = g_menu_new();
  GMenu *section = g_menu_new();
  auto *actions = g_simple_action_group_new();

  for (size_t i = 0; i < pending->request.entries.size(); i++) {
    const MenuEntry &entry = pending->request.entries[i];
    if (entry.isSeparator()) {
      g_menu_append_section(model, nullptr, G_MENU_MODEL(section));
      g_object_unref(section);
      section = g_menu_new();
      continue;
    }

    const std::string name = "item" + std::to_string(i);
    auto *action = g_simple_action_new(name.c_str(), nullptr);
    g_simple_action_set_enabled(action, entry.enabled ? TRUE : FALSE);
    g_signal_connect(action, "activate", G_CALLBACK(onMenuItemActivated), pending);
    g_action_map_add_action(G_ACTION_MAP(actions), G_ACTION(action));
    g_object_unref(action);

    GMenuItem *item = g_menu_item_new(entry.label.c_str(), ("menu." + name).c_str());
    const std::string accelerator = toGtkAccelerator(entry.shortcut);
    if (!accelerator.empty()) {
      g_menu_item_set_attribute(item, "accel", "s", accelerator.c_str());
    }
    g_menu_append_item(section, item);
    g_object_unref(item);
  }
  g_menu_append_section(model, nullptr, G_MENU_MODEL(section));
  g_object_unref(section);

  GtkWidget *popover = gtk_popover_menu_new_from_model(G_MENU_MODEL(model));
  g_object_unref(model);
  pending->popover = popover;

  gtk_widget_insert_action_group(popover, "menu", G_ACTION_GROUP(actions));
  g_object_unref(actions);

  gtk_widget_set_parent(popover, anchor);
  gtk_popover_set_has_arrow(GTK_POPOVER(popover), FALSE);
  if (pending->request.x >= 0.0 && pending->request.y >= 0.0) {
    const GdkRectangle at = {static_cast<int>(pending->request.x),
                             static_cast<int>(pending->request.y),
                             1,
                             1};
    gtk_popover_set_pointing_to(GTK_POPOVER(popover), &at);
  }
  g_signal_connect(popover, "closed", G_CALLBACK(onMenuClosed), pending);
  gtk_popover_popup(GTK_POPOVER(popover));
  return G_SOURCE_REMOVE;
}

} // namespace

void showMenu(const MenuRequest &request, MenuCallback onChosen) {
  auto *pending = new PendingMenu{request, std::move(onChosen), nullptr, false};
  // Onto the GTK main thread, for the same reason showAlert is: this can be
  // called from the JavaScript thread, and a widget may only be made here.
  g_idle_add_full(G_PRIORITY_DEFAULT, showMenuOnMainThread, pending, nullptr);
}

void postDelayed(double milliseconds, std::function<void()> work) {
  // Heap-allocated because g_timeout_add takes a void*, and freed by the
  // destroy notify whether the callback ran or the source was removed.
  auto *held = new std::function<void()>(std::move(work));
  g_timeout_add_full(
      G_PRIORITY_DEFAULT,
      static_cast<guint>(milliseconds),
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return G_SOURCE_REMOVE;
      },
      held,
      [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}

void postToUiThread(std::function<void()> work) {
  auto *held = new std::function<void()>(std::move(work));
  g_idle_add_full(
      G_PRIORITY_DEFAULT,
      [](gpointer data) -> gboolean {
        (*static_cast<std::function<void()> *>(data))();
        return G_SOURCE_REMOVE;
      },
      held,
      [](gpointer data) { delete static_cast<std::function<void()> *>(data); });
}

bool isUiThread() {
  // The thread running the default main context is the one GTK draws from, and
  // asking that rather than remembering a thread id keeps this true in a test
  // binary that never starts a loop -- where the honest answer is "no".
  return g_main_context_is_owner(g_main_context_default()) != 0;
}

} // namespace basalt
