// Desktop notifications on Windows.
//
// Two APIs could do this and only one of them is reachable from here.
//
// `ToastNotificationManager` is the modern one and it is WinRT: reaching it
// from a plain Win32 executable means C++/WinRT or the ABI headers, a manifest,
// and an activator COM class registered so a click on the toast can come back.
// That is a real piece of work and it is not this.
//
// `Shell_NotifyIcon` with `NIF_INFO` is the other, and on Windows 10 and later
// it is not the XP balloon it used to be: the shell turns it into a toast,
// shows it in the same place, and keeps it in the Action Center. What it needs
// is a tray icon to come from -- which this file therefore owns, hidden or not
// -- and, to be attributed to this application rather than to "a program",
// an AppUserModelID with a Start Menu shortcut behind it. `Win32Packaging.h`
// does that half.
//
// So the support check is not "am I a packaged app" but "did packaging
// succeed", which is the honest question: an app with no identity file beside
// its bundle gets no id, no shortcut and no notifications, and says so.
//
// ## What this cannot do
//
// A balloon carries no identifier of its own. `Shell_NotifyIcon` shows one at a
// time per icon, replacing whatever was there, so `dismissNotification` can
// only take down what is currently shown and `presentedNotifications` can only
// answer what this process last asked for -- which is what the GTK host's map
// answers too, for a different reason. A notification a person has already
// dismissed still counts as presented until something replaces it. Recorded in
// docs/BACKLOG.md rather than papered over.

#include "Notifications.h"

#include "AppIdentity.h"
#include "Win32Packaging.h"
#include "Win32Strings.h"

#include <windows.h>

#include <shellapi.h>

#include <cwchar>
#include <mutex>
#include <string>
#include <vector>

namespace basalt {

// Win32Strings.h puts it in basalt::win32; this file is the notification seam,
// which is plain `basalt`. PlatformServicesWin32.cpp does the same.
using win32::widen;

namespace {

constexpr UINT kTrayIconId = 1;

struct TrayIcon {
  HWND window{nullptr};
  bool added{false};
};

std::mutex &lock() {
  static std::mutex value;
  return value;
}

TrayIcon &tray() {
  static TrayIcon icon;
  return icon;
}

// What was last asked for, and not yet replaced. See the header for why this
// is the best answer available.
std::string &showing() {
  static std::string identifier;
  return identifier;
}

// A message-only window to own the tray icon.
//
// Message-only rather than the app's own window: the icon has to outlive
// whatever the app is doing with its window, and a HWND_MESSAGE window is never
// shown, never painted and never in the taskbar. It is created on first use, so
// an app that never notifies never has a tray icon.
HWND ensureOwnerWindow() {
  TrayIcon &icon = tray();
  if (icon.window != nullptr) {
    return icon.window;
  }

  static const wchar_t *kClassName = L"BasaltNotificationOwner";
  WNDCLASSEXW description{};
  description.cbSize = sizeof(description);
  description.lpfnWndProc = DefWindowProcW;
  description.hInstance = GetModuleHandleW(nullptr);
  description.lpszClassName = kClassName;
  // Registering twice is not an error worth reporting: the class is process
  // wide and this is the only caller.
  RegisterClassExW(&description);

  icon.window = CreateWindowExW(0,
                                kClassName,
                                L"",
                                0,
                                0,
                                0,
                                0,
                                0,
                                HWND_MESSAGE,
                                nullptr,
                                GetModuleHandleW(nullptr),
                                nullptr);
  return icon.window;
}

// Adds the tray icon if it is not there. Hidden from the person as much as
// Windows allows: it carries no menu and its tooltip is the app's name.
bool ensureTrayIcon() {
  TrayIcon &icon = tray();
  if (icon.added) {
    return true;
  }
  HWND owner = ensureOwnerWindow();
  if (owner == nullptr) {
    return false;
  }

  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = owner;
  data.uID = kTrayIconId;
  data.uFlags = NIF_ICON | NIF_TIP;
  data.hIcon = LoadIcon(nullptr, IDI_APPLICATION);
  const std::wstring name = widen(appIdentity().name);
  wcsncpy_s(data.szTip, name.c_str(), _TRUNCATE);

  if (!Shell_NotifyIconW(NIM_ADD, &data)) {
    return false;
  }
  // Version 4 is what makes the shell treat NIF_INFO as a toast rather than as
  // the old balloon, and what makes the string limits the modern ones.
  data.uVersion = NOTIFYICON_VERSION_4;
  Shell_NotifyIconW(NIM_SETVERSION, &data);

  icon.added = true;
  return true;
}

} // namespace

NotificationSupport notificationSupport() {
  if (!win32::hasAppUserModelId()) {
    return {false,
            "this app has no AppUserModelID, so Windows will not attribute a "
            "notification to it -- run it through `react-native run-windows`, "
            "which writes the identity the host needs"};
  }
  return {true, {}};
}

bool showNotification(const std::string &identifier, const NotificationContent &content) {
  if (!win32::hasAppUserModelId()) {
    return false;
  }

  const std::lock_guard<std::mutex> guard(lock());
  if (!ensureTrayIcon()) {
    return false;
  }

  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = tray().window;
  data.uID = kTrayIconId;
  data.uFlags = NIF_INFO;
  data.dwInfoFlags = NIIF_INFO;

  // React Native's notification has a title, a subtitle and a body; a toast
  // here has a title and a body. The subtitle goes on the front of the body,
  // which is where it reads -- dropping it would silently lose what an app
  // wrote.
  std::string body = content.subtitle;
  if (!body.empty() && !content.body.empty()) {
    body += "\n";
  }
  body += content.body;

  const std::wstring title = widen(content.title);
  const std::wstring text = widen(body);
  wcsncpy_s(data.szInfoTitle, title.c_str(), _TRUNCATE);
  wcsncpy_s(data.szInfo, text.c_str(), _TRUNCATE);

  if (!Shell_NotifyIconW(NIM_MODIFY, &data)) {
    return false;
  }
  showing() = identifier;
  return true;
}

bool dismissNotification(const std::string &identifier) {
  const std::lock_guard<std::mutex> guard(lock());
  if (showing() != identifier) {
    return false;
  }
  showing().clear();

  // An empty info string is how a balloon is taken down; there is no "remove
  // this one" call, which is why only the current one can be dismissed.
  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = tray().window;
  data.uID = kTrayIconId;
  data.uFlags = NIF_INFO;
  Shell_NotifyIconW(NIM_MODIFY, &data);
  return true;
}

void dismissAllNotifications() {
  const std::lock_guard<std::mutex> guard(lock());
  if (showing().empty()) {
    return;
  }
  showing().clear();

  NOTIFYICONDATAW data{};
  data.cbSize = sizeof(data);
  data.hWnd = tray().window;
  data.uID = kTrayIconId;
  data.uFlags = NIF_INFO;
  Shell_NotifyIconW(NIM_MODIFY, &data);
}

std::vector<std::string> presentedNotifications() {
  const std::lock_guard<std::mutex> guard(lock());
  if (showing().empty()) {
    return {};
  }
  return {showing()};
}

} // namespace basalt
