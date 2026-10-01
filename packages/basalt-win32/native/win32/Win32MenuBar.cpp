// The Windows half of core/MenuModel.h: a real menu bar, in the window.
//
// Windows is the middle case. A menu bar is conventional rather than required,
// it lives inside the window rather than in a system bar, and -- unlike macOS
// -- nothing about editing depends on it: an EDIT control implements Ctrl+C,
// Ctrl+V and Ctrl+Z itself, so the Edit menu is a convenience rather than the
// thing that makes copying work.
//
// Which is why the roles here are posted to the focused control rather than
// sent down a responder chain. Windows has no responder chain; what it has is
// `WM_COPY` and friends, which every standard edit control answers and which
// reach whichever one has focus.
//
// The host forwards `WM_COMMAND` here. A menu bar costs client area, so
// installing one resizes the surface -- which the host already handles, because
// the same thing happens when a window is resized for any other reason.

#include "Win32MenuBar.h"

#include "MenuModel.h"
#include "WindowControl.h"
#include "Win32Strings.h"

#include <windows.h>

#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

namespace {

// Command ids. An app's own item carries its own id and a role needs one of
// these, so they are kept apart by range: an app's ids come from JavaScript and
// start at 1, and roles start high enough that the two cannot meet.
constexpr UINT kRoleBase = 0xE000;

struct Role {
  const char *name;
  UINT command;
};

constexpr Role kRoles[] = {
    {"quit", kRoleBase + 1},
    {"undo", kRoleBase + 2},
    {"redo", kRoleBase + 3},
    {"cut", kRoleBase + 4},
    {"copy", kRoleBase + 5},
    {"paste", kRoleBase + 6},
    {"delete", kRoleBase + 7},
    {"selectAll", kRoleBase + 8},
    {"minimize", kRoleBase + 9},
    {"zoom", kRoleBase + 10},
    {"close", kRoleBase + 11},
    {"togglefullscreen", kRoleBase + 12},
    // "about" has no standard Windows action. An app that wants one shows its
    // own window, which is an app's own item rather than a role.
};

UINT commandForRole(const std::string &role) {
  for (const Role &known : kRoles) {
    if (role == known.name) {
      return known.command;
    }
  }
  return 0;
}

// The platform's own word, plus the shortcut it answers to. Windows draws an
// accelerator as the text after a tab; nothing here registers one, because the
// EDIT control already answers these keys.
std::wstring labelForRole(const std::string &role, const std::string &label) {
  if (!label.empty()) {
    return win32::widen(label);
  }
  if (role == "quit") {
    return L"Exit";
  }
  if (role == "undo") {
    return L"Undo\tCtrl+Z";
  }
  if (role == "redo") {
    return L"Redo\tCtrl+Y";
  }
  if (role == "cut") {
    return L"Cut\tCtrl+X";
  }
  if (role == "copy") {
    return L"Copy\tCtrl+C";
  }
  if (role == "paste") {
    return L"Paste\tCtrl+V";
  }
  if (role == "delete") {
    return L"Delete";
  }
  if (role == "selectAll") {
    return L"Select All\tCtrl+A";
  }
  if (role == "minimize") {
    return L"Minimize";
  }
  if (role == "zoom") {
    return L"Maximize";
  }
  if (role == "close") {
    return L"Close";
  }
  if (role == "togglefullscreen") {
    return L"Full Screen";
  }
  return {};
}

std::function<void(int)> &handler() {
  static std::function<void(int)> value;
  return value;
}

HMENU &installed() {
  static HMENU menu = nullptr;
  return menu;
}

void addItems(HMENU menu, const std::vector<MenuItemModel> &items);

void addItem(HMENU menu, const MenuItemModel &item) {
  if (item.separator) {
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    return;
  }

  const UINT flags = item.enabled ? MF_ENABLED : MF_GRAYED;

  if (!item.submenu.empty()) {
    HMENU submenu = CreatePopupMenu();
    if (submenu == nullptr) {
      return;
    }
    addItems(submenu, item.submenu);
    AppendMenuW(menu,
                MF_POPUP | flags,
                reinterpret_cast<UINT_PTR>(submenu),
                win32::widen(item.label).c_str());
    return;
  }

  if (!item.role.empty()) {
    const UINT command = commandForRole(item.role);
    const std::wstring label = labelForRole(item.role, item.label);
    // An unknown role is dropped: a menu item that does nothing is worse than
    // one that is not there.
    if (command != 0 && !label.empty()) {
      AppendMenuW(menu, MF_STRING | flags, command, label.c_str());
    }
    return;
  }

  if (item.id <= 0) {
    return;
  }
  std::wstring label = win32::widen(item.label);
  if (!item.accelerator.empty()) {
    label += L"\t";
    label += win32::widen(item.accelerator);
  }
  AppendMenuW(menu, MF_STRING | flags, static_cast<UINT_PTR>(item.id), label.c_str());
}

void addItems(HMENU menu, const std::vector<MenuItemModel> &items) {
  for (const MenuItemModel &item : items) {
    addItem(menu, item);
  }
}

} // namespace

bool applicationMenuSupported() {
  return true;
}

namespace {

void describeInto(std::string &out, HMENU menu, int depth) {
  const int count = GetMenuItemCount(menu);
  for (int i = 0; i < count; i++) {
    wchar_t text[512] = {0};
    MENUITEMINFOW info{};
    info.cbSize = sizeof(info);
    info.fMask = MIIM_STRING | MIIM_SUBMENU | MIIM_STATE | MIIM_FTYPE;
    info.dwTypeData = text;
    info.cch = static_cast<UINT>(std::size(text) - 1);
    if (!GetMenuItemInfoW(menu, static_cast<UINT>(i), TRUE, &info)) {
      continue;
    }

    out.append(static_cast<size_t>(depth) * 2, ' ');
    if ((info.fType & MFT_SEPARATOR) != 0) {
      out += "-\n";
      continue;
    }
    // The accelerator is the text after a tab, which is how Windows stores one;
    // it reads the same way the macOS dump's bracket does.
    std::wstring label(text);
    const size_t tab = label.find(L'\t');
    if (tab != std::wstring::npos) {
      out += win32::narrow(label.substr(0, tab));
      out += " [";
      out += win32::narrow(label.substr(tab + 1));
      out += "]";
    } else {
      out += win32::narrow(label);
    }
    if ((info.fState & MFS_DISABLED) != 0) {
      out += " (disabled)";
    }
    out += "\n";
    if (info.hSubMenu != nullptr) {
      describeInto(out, info.hSubMenu, depth + 1);
    }
  }
}

} // namespace

std::string describeApplicationMenu() {
  std::string out;
  if (installed() != nullptr) {
    describeInto(out, installed(), 0);
  }
  return out;
}

void setApplicationMenu(const MenuModel &menu, std::function<void(int)> onChosen) {
  HWND window = GetActiveWindow();
  if (window == nullptr) {
    window = GetForegroundWindow();
  }
  if (window == nullptr) {
    return;
  }

  handler() = std::move(onChosen);

  // An empty menu takes the bar away, which is what Windows' own default is:
  // unlike macOS there is nothing an application needs a menu bar to do.
  if (menu.empty()) {
    SetMenu(window, nullptr);
    if (installed() != nullptr) {
      DestroyMenu(installed());
      installed() = nullptr;
    }
    DrawMenuBar(window);
    return;
  }

  HMENU bar = CreateMenu();
  if (bar == nullptr) {
    return;
  }
  for (const MenuItemModel &top : menu) {
    // A bare command in a menu bar is not a thing any desktop has.
    if (top.submenu.empty()) {
      continue;
    }
    HMENU submenu = CreatePopupMenu();
    if (submenu == nullptr) {
      continue;
    }
    addItems(submenu, top.submenu);
    AppendMenuW(bar,
                MF_POPUP,
                reinterpret_cast<UINT_PTR>(submenu),
                win32::widen(top.label).c_str());
  }

  SetMenu(window, bar);
  // Destroyed after the new one is in: a menu still attached to a window
  // cannot be freed, and freeing the old one first would leave the window
  // pointing at nothing for the length of a call.
  if (installed() != nullptr) {
    DestroyMenu(installed());
  }
  installed() = bar;
  DrawMenuBar(window);
}

namespace win32 {

bool handleMenuCommand(unsigned int command) {
  if (command < kRoleBase) {
    // An app's own item. Zero is "not from a menu", which WM_COMMAND also uses
    // for accelerators and controls.
    if (command == 0 || !handler()) {
      return false;
    }
    handler()(static_cast<int>(command));
    return true;
  }

  HWND window = GetActiveWindow();
  HWND focused = GetFocus();
  switch (command) {
    case kRoleBase + 1:
      PostMessageW(window, WM_CLOSE, 0, 0);
      return true;
    // Posted to whatever has focus, which for these is a real EDIT control and
    // answers every one of them itself. Windows has no responder chain; this is
    // the equivalent.
    case kRoleBase + 2:
      SendMessageW(focused, WM_UNDO, 0, 0);
      return true;
    case kRoleBase + 3:
      // EDIT has no redo. Reported as handled anyway: the alternative is the
      // key travelling on to something that will do something else with it.
      return true;
    case kRoleBase + 4:
      SendMessageW(focused, WM_CUT, 0, 0);
      return true;
    case kRoleBase + 5:
      SendMessageW(focused, WM_COPY, 0, 0);
      return true;
    case kRoleBase + 6:
      SendMessageW(focused, WM_PASTE, 0, 0);
      return true;
    case kRoleBase + 7:
      SendMessageW(focused, WM_CLEAR, 0, 0);
      return true;
    case kRoleBase + 8:
      SendMessageW(focused, EM_SETSEL, 0, -1);
      return true;
    case kRoleBase + 9:
      ShowWindow(window, SW_MINIMIZE);
      return true;
    case kRoleBase + 10:
      ShowWindow(window, IsZoomed(window) ? SW_RESTORE : SW_MAXIMIZE);
      return true;
    case kRoleBase + 11:
      PostMessageW(window, WM_CLOSE, 0, 0);
      return true;
    case kRoleBase + 12:
      setWindowFullScreen(!windowBounds().fullScreen);
      return true;
    default:
      return false;
  }
}

} // namespace win32
} // namespace basalt
