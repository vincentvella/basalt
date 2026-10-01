// The Windows half of core/WindowControl.h.
//
// The only one of the three where the coordinates already agree with React
// Native's -- top-left origin, y growing downwards -- and the only one where
// every function can be answered honestly.
//
// What it does have to be careful about is the difference between a window's
// frame and its client area. `GetWindowRect` is the frame, including the border
// and the title bar; `setWindowSize` is given the size an app wants, and an app
// asking for 900x700 means its own content. So the size is adjusted outwards by
// whatever the frame costs, which is what `AdjustWindowRectEx` is for -- and
// which a window with a hidden title bar costs nothing for, so the same call
// answers both cases.

#include <vector>
#include "WindowControl.h"

#include <windows.h>

// After windows.h, which it depends on: GetDpiForMonitor, for a per-display
// scale factor. GetDpiForWindow would answer for the window's monitor only.
#include <shellscalingapi.h>

namespace basalt {

namespace {


HWND window() {
  HWND active = GetActiveWindow();
  return active != nullptr ? active : GetForegroundWindow();
}

} // namespace

// Every monitor, through the callback API that is the only way to ask.
//
// `MONITORINFOEXW` carries both rectangles -- `rcMonitor` is the glass and
// `rcWork` is what the taskbar leaves -- which is why the work area needs no
// second call here and cannot be had at all on GTK.
//
// The coordinates are already what this platform means by desktop
// coordinates: Windows anchors them at the primary monitor's top-left and
// grows down, which is the convention the other two are converted into.
std::vector<DisplayInfo> displays() {
  std::vector<DisplayInfo> found;
  EnumDisplayMonitors(
      nullptr,
      nullptr,
      [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto *out = reinterpret_cast<std::vector<DisplayInfo> *>(data);
        MONITORINFOEXW info{};
        info.cbSize = sizeof(info);
        if (!GetMonitorInfoW(monitor, &info)) {
          return TRUE;
        }

        DisplayInfo entry;
        entry.x = info.rcMonitor.left;
        entry.y = info.rcMonitor.top;
        entry.width = info.rcMonitor.right - info.rcMonitor.left;
        entry.height = info.rcMonitor.bottom - info.rcMonitor.top;
        entry.workX = info.rcWork.left;
        entry.workY = info.rcWork.top;
        entry.workWidth = info.rcWork.right - info.rcWork.left;
        entry.workHeight = info.rcWork.bottom - info.rcWork.top;

        // 96 DPI is 100%, which is what makes this a ratio rather than a
        // number of dots. A display at 150% answers 144 and reports 1.5.
        UINT dpiX = 96;
        UINT dpiY = 96;
        if (SUCCEEDED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY)) && dpiX > 0) {
          entry.scaleFactor = static_cast<double>(dpiX) / 96.0;
        }
        entry.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

        // Primary first, to match the order the other hosts report and the
        // order the header promises. EnumDisplayMonitors does not guarantee
        // one.
        if (entry.primary) {
          out->insert(out->begin(), entry);
        } else {
          out->push_back(entry);
        }
        return TRUE;
      },
      reinterpret_cast<LPARAM>(&found));
  return found;
}

PointerPosition pointerPosition() {
  PointerPosition where;
  POINT point{};
  if (GetCursorPos(&point)) {
    where.x = point.x;
    where.y = point.y;
    where.known = true;
  }
  return where;
}

WindowBounds windowBounds() {
  WindowBounds bounds;
  HWND target = window();
  if (target == nullptr) {
    return bounds;
  }

  RECT frame{};
  if (GetWindowRect(target, &frame)) {
    bounds.x = frame.left;
    bounds.y = frame.top;
  }
  // The client area, which is what an app laid out and what the other two
  // hosts report: a frame's size includes a border the app never drew in.
  RECT client{};
  if (GetClientRect(target, &client)) {
    bounds.width = client.right - client.left;
    bounds.height = client.bottom - client.top;
  }

  bounds.maximized = IsZoomed(target) != FALSE;

  // Full screen is not a state Windows has; it is a window with no border
  // filling a monitor, which is how every application does it. So the question
  // is asked the way the answer is made: no title bar, and exactly the size of
  // the monitor it is on.
  if (const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST)) {
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (GetMonitorInfoW(monitor, &info)) {
      const LONG style = GetWindowLongW(target, GWL_STYLE);
      bounds.fullScreen = (style & WS_OVERLAPPEDWINDOW) == 0 &&
          frame.left == info.rcMonitor.left && frame.top == info.rcMonitor.top &&
          frame.right == info.rcMonitor.right && frame.bottom == info.rcMonitor.bottom;
    }
  }
  return bounds;
}

void setWindowSize(double width, double height) {
  HWND target = window();
  if (target == nullptr || width <= 0.0 || height <= 0.0) {
    return;
  }
  // Outwards from the client size the app asked for to the frame size Windows
  // positions. A borderless window adjusts by nothing, so this is right for
  // both title-bar styles.
  RECT desired{0, 0, static_cast<LONG>(width), static_cast<LONG>(height)};
  const LONG style = GetWindowLongW(target, GWL_STYLE);
  const LONG exStyle = GetWindowLongW(target, GWL_EXSTYLE);
  AdjustWindowRectEx(&desired, static_cast<DWORD>(style), FALSE, static_cast<DWORD>(exStyle));

  SetWindowPos(target,
               nullptr,
               0,
               0,
               desired.right - desired.left,
               desired.bottom - desired.top,
               SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void setWindowPosition(double x, double y) {
  HWND target = window();
  if (target == nullptr) {
    return;
  }
  SetWindowPos(target,
               nullptr,
               static_cast<int>(x),
               static_cast<int>(y),
               0,
               0,
               SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void centerWindow() {
  HWND target = window();
  if (target == nullptr) {
    return;
  }
  RECT frame{};
  if (!GetWindowRect(target, &frame)) {
    return;
  }
  // The work area of the monitor the window is on, not the primary one and not
  // the whole monitor: centring into the space the taskbar leaves is what a
  // person means, and what every other application does.
  const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
  MONITORINFO info{};
  info.cbSize = sizeof(info);
  if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) {
    return;
  }

  const LONG width = frame.right - frame.left;
  const LONG height = frame.bottom - frame.top;
  const LONG x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
  const LONG y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
  SetWindowPos(target, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void setWindowFullScreen(bool fullScreen) {
  HWND target = window();
  if (target == nullptr) {
    return;
  }

  // Where the window was before it filled the screen, so that leaving puts it
  // back. One window, so one saved rect; see core/WindowControl.h on that
  // limit.
  static RECT restore{};
  static LONG restoreStyle = 0;

  const LONG style = GetWindowLongW(target, GWL_STYLE);
  const bool already = (style & WS_OVERLAPPEDWINDOW) == 0;
  if (already == fullScreen) {
    return;
  }

  if (fullScreen) {
    GetWindowRect(target, &restore);
    restoreStyle = style;

    const HMONITOR monitor = MonitorFromWindow(target, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (monitor == nullptr || !GetMonitorInfoW(monitor, &info)) {
      return;
    }
    SetWindowLongW(target, GWL_STYLE, style & ~WS_OVERLAPPEDWINDOW);
    // The whole monitor rather than its work area: filling the screen means
    // over the taskbar, which is the difference between this and maximising.
    SetWindowPos(target,
                 HWND_TOP,
                 info.rcMonitor.left,
                 info.rcMonitor.top,
                 info.rcMonitor.right - info.rcMonitor.left,
                 info.rcMonitor.bottom - info.rcMonitor.top,
                 SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    return;
  }

  SetWindowLongW(target, GWL_STYLE, restoreStyle != 0 ? restoreStyle : (style | WS_OVERLAPPEDWINDOW));
  SetWindowPos(target,
               nullptr,
               restore.left,
               restore.top,
               restore.right - restore.left,
               restore.bottom - restore.top,
               SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
}

void applyWindowSizeLimits() {
  HWND target = window();
  if (target == nullptr) {
    return;
  }
  // Nothing to set. Windows asks rather than being told: WM_GETMINMAXINFO
  // arrives whenever the window manager wants the answer, and the host replies
  // from `windowSizeLimits()`. All this does is make it ask again, so that a
  // limit set while the window is already too big takes effect now rather than
  // the next time somebody drags a corner.
  RECT frame{};
  if (!GetWindowRect(target, &frame)) {
    return;
  }
  SetWindowPos(target,
               nullptr,
               frame.left,
               frame.top,
               frame.right - frame.left,
               frame.bottom - frame.top,
               SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void setWindowResizable(bool resizable) {
  HWND target = window();
  if (target == nullptr) {
    return;
  }
  LONG style = GetWindowLongW(target, GWL_STYLE);
  // The sizing border and the maximise button together. Leaving the button on a
  // window that cannot be sized would be an enabled control that does nothing,
  // which is the same mistake as a greyed-out one that works.
  if (resizable) {
    style |= (WS_THICKFRAME | WS_MAXIMIZEBOX);
  } else {
    style &= ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
  }
  SetWindowLongW(target, GWL_STYLE, style);
  // SWP_FRAMECHANGED, or the border is recalculated only the next time
  // something else moves the window -- so the style changes and the frame does
  // not, until it suddenly does.
  SetWindowPos(target,
               nullptr,
               0,
               0,
               0,
               0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE |
                   SWP_FRAMECHANGED);
}

void setWindowAlwaysOnTop(bool alwaysOnTop) {
  HWND target = window();
  if (target == nullptr) {
    return;
  }
  SetWindowPos(target,
               alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
               0,
               0,
               0,
               0,
               SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

WindowCapabilities windowCapabilities() {
  WindowCapabilities capabilities;
  capabilities.position = true;
  capabilities.minimumSize = true;
  capabilities.maximumSize = true;
  capabilities.resizable = true;
  capabilities.alwaysOnTop = true;
  return capabilities;
}

} // namespace basalt
