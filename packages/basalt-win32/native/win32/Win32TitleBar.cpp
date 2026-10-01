#include "Win32TitleBar.h"

#include "RnWin32View.h"
#include "Win32Strings.h"

#include <d2d1.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace basalt {

namespace {

using win32::CaptionButton;

// The DWM attributes, by value: older SDK headers do not name the colour ones,
// and the numbers are Windows' rather than the header's to change.
constexpr DWORD kUseImmersiveDarkMode = 20; // DWMWA_USE_IMMERSIVE_DARK_MODE
constexpr DWORD kBorderColor = 34;          // DWMWA_BORDER_COLOR
constexpr DWORD kCaptionColor = 35;         // DWMWA_CAPTION_COLOR
constexpr DWORD kTextColor = 36;            // DWMWA_TEXT_COLOR
constexpr COLORREF kColorDefault = 0xFFFFFFFF; // DWMWA_COLOR_DEFAULT

UINT dpiOf(HWND window) {
  const UINT dpi = window != nullptr ? GetDpiForWindow(window) : 96;
  return dpi == 0 ? 96 : dpi;
}

// The resize band along an edge: the frame and the padding Windows adds to it.
int resizeBorder(UINT dpi) {
  return GetSystemMetricsForDpi(SM_CYFRAME, dpi) + GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
}

LRESULT hitCodeFor(CaptionButton button) {
  switch (button) {
    case CaptionButton::Minimize:
      return HTMINBUTTON;
    case CaptionButton::Maximize:
      return HTMAXBUTTON;
    case CaptionButton::Close:
      return HTCLOSE;
    case CaptionButton::None:
      break;
  }
  return HTNOWHERE;
}

CaptionButton buttonForHitCode(WPARAM code) {
  switch (code) {
    case HTMINBUTTON:
      return CaptionButton::Minimize;
    case HTMAXBUTTON:
      return CaptionButton::Maximize;
    case HTCLOSE:
      return CaptionButton::Close;
    default:
      return CaptionButton::None;
  }
}

D2D1_COLOR_F toColor(COLORREF color, float alpha = 1.0f) {
  return D2D1::ColorF(static_cast<float>(GetRValue(color)) / 255.0f,
                      static_cast<float>(GetGValue(color)) / 255.0f,
                      static_cast<float>(GetBValue(color)) / 255.0f,
                      alpha);
}

void setDwmColor(HWND window, DWORD attribute, std::optional<COLORREF> color) {
  const COLORREF value = color.value_or(kColorDefault);
  // Fails on Windows 10, which has no such attributes. That is the documented
  // way to find out, and a caption that stays the system's colour is the right
  // outcome there.
  DwmSetWindowAttribute(window, attribute, &value, sizeof(value));
}

} // namespace

Win32TitleBar &titleBar() {
  static Win32TitleBar instance;
  return instance;
}

void Win32TitleBar::attach(HWND window) {
  window_ = window;
  applyTitle();
  const BOOL dark = dark_ ? TRUE : FALSE;
  DwmSetWindowAttribute(window_, kUseImmersiveDarkMode, &dark, sizeof(dark));
  applyColors();
  publishMetrics();
}

void Win32TitleBar::setDefaultTitle(const std::string &title) {
  defaultTitle_ = title;
  applyTitle();
}

void Win32TitleBar::setTitle(std::optional<std::string> title) {
  title_ = std::move(title);
  applyTitle();
}

void Win32TitleBar::applyTitle() {
  if (window_ == nullptr) {
    return;
  }
  const std::string &title = title_.has_value() && !title_->empty() ? *title_ : defaultTitle_;
  SetWindowTextW(window_, win32::widen(title).c_str());
}

void Win32TitleBar::setDarkMode(bool dark) {
  if (dark == dark_) {
    return;
  }
  dark_ = dark;
  if (window_ == nullptr) {
    return;
  }
  const BOOL value = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(window_, kUseImmersiveDarkMode, &value, sizeof(value));
  redrawFrame();
  invalidateButtons();
}

void Win32TitleBar::setColors(std::optional<COLORREF> caption,
                              std::optional<COLORREF> text,
                              std::optional<COLORREF> border) {
  captionColor_ = caption;
  textColor_ = text;
  borderColor_ = border;
  applyColors();
  invalidateButtons();
}

void Win32TitleBar::applyColors() {
  if (window_ == nullptr) {
    return;
  }
  setDwmColor(window_, kCaptionColor, captionColor_);
  setDwmColor(window_, kTextColor, textColor_);
  setDwmColor(window_, kBorderColor, borderColor_);
}

void Win32TitleBar::setStyle(TitleBarStyle style) {
  if (style == style_) {
    return;
  }
  style_ = style;
  hovered_ = CaptionButton::None;
  pressed_ = CaptionButton::None;
  // WM_NCCALCSIZE is only asked again when the frame is said to have changed,
  // which is what gives the caption back to the client area or takes it away.
  redrawFrame();
  publishMetrics();
  invalidateButtons();
}

void Win32TitleBar::redrawFrame() {
  if (window_ != nullptr) {
    SetWindowPos(window_,
                 nullptr,
                 0,
                 0,
                 0,
                 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  }
}

win32::CaptionMetrics Win32TitleBar::captionMetrics() const {
  return win32::captionMetricsForDpi(dpiOf(window_));
}

TitleBarMetrics Win32TitleBar::metrics() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return metrics_;
}

void Win32TitleBar::setMetricsListener(MetricsListener listener) {
  std::lock_guard<std::mutex> lock(mutex_);
  listener_ = std::move(listener);
}

void Win32TitleBar::refreshMetrics() {
  publishMetrics();
}

void Win32TitleBar::publishMetrics() {
  TitleBarMetrics next;
  next.style = style_;
  if (style_ == TitleBarStyle::Hidden) {
    const auto caption = captionMetrics();
    next.height = caption.height;
    next.buttonsWidth = 3 * caption.buttonWidth;
  }

  MetricsListener listener;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool changed = next.height != metrics_.height ||
        next.buttonsWidth != metrics_.buttonsWidth || next.style != metrics_.style;
    metrics_ = next;
    if (!changed) {
      return;
    }
    listener = listener_;
  }
  if (listener) {
    listener(next);
  }
}

bool Win32TitleBar::handleNcCalcSize(WPARAM wparam, LPARAM lparam, LRESULT &result) {
  if (style_ != TitleBarStyle::Hidden || window_ == nullptr || wparam != TRUE) {
    return false;
  }
  auto *params = reinterpret_cast<NCCALCSIZE_PARAMS *>(lparam);
  const LONG top = params->rgrc[0].top;

  // Windows takes its usual frame off all four sides, and then the top is given
  // back: the side and bottom borders stay -- and with them resizing from those
  // edges, and the window's shadow -- while the caption becomes client area.
  result = DefWindowProc(window_, WM_NCCALCSIZE, wparam, lparam);
  params->rgrc[0].top = top;

  // A maximised window is placed with its frame hanging past the edges of the
  // monitor. The sides and bottom are still taken off above, but the top would
  // now start that far above the screen, and the app's header with it.
  if (IsZoomed(window_)) {
    params->rgrc[0].top += resizeBorder(dpiOf(window_));
  }
  return true;
}

bool Win32TitleBar::handleNcHitTest(LPARAM lparam, win32::RnWin32View *root, LRESULT &result) {
  if (style_ != TitleBarStyle::Hidden || window_ == nullptr) {
    return false;
  }

  // The side and bottom edges are still Windows' own frame, and so is its
  // answer about them.
  const LRESULT byWindows = DefWindowProc(window_, WM_NCHITTEST, 0, lparam);
  if (byWindows != HTCLIENT) {
    result = byWindows;
    return true;
  }

  POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
  ScreenToClient(window_, &point);
  RECT client{};
  GetClientRect(window_, &client);
  const float width = static_cast<float>(client.right - client.left);
  const UINT dpi = dpiOf(window_);

  // The top edge became client area when the caption was given back, so
  // resizing from it has to be offered here. Not while maximised, when there
  // is no edge to drag.
  if (!IsZoomed(window_)) {
    const int border = resizeBorder(dpi);
    if (point.y < border) {
      if (point.x < border) {
        result = HTTOPLEFT;
      } else if (point.x >= static_cast<LONG>(width) - border) {
        result = HTTOPRIGHT;
      } else {
        result = HTTOP;
      }
      return true;
    }
  }

  const CaptionButton button = win32::captionButtonAt(
      static_cast<float>(point.x), static_cast<float>(point.y), width, captionMetrics());
  if (button != CaptionButton::None) {
    result = hitCodeFor(button);
    return true;
  }

  if (root != nullptr &&
      win32::isTitleBarDragRegionAt(root, static_cast<float>(point.x), static_cast<float>(point.y))) {
    result = HTCAPTION;
    return true;
  }

  result = HTCLIENT;
  return true;
}

bool Win32TitleBar::handleNcMouse(UINT message, WPARAM wparam, LRESULT &result) {
  if (style_ != TitleBarStyle::Hidden || window_ == nullptr) {
    return false;
  }

  switch (message) {
    case WM_NCMOUSEMOVE: {
      const CaptionButton button = buttonForHitCode(wparam);
      if (button != hovered_) {
        hovered_ = button;
        invalidateButtons();
      }
      // Leaving the non-client area for the client area is otherwise silent,
      // and a button would stay lit after the pointer had gone.
      if (!trackingLeave_) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE | TME_NONCLIENT, window_, 0};
        trackingLeave_ = TrackMouseEvent(&track) != FALSE;
      }
      if (button == CaptionButton::None) {
        return false;
      }
      result = 0;
      return true;
    }

    case WM_NCMOUSELEAVE:
      trackingLeave_ = false;
      if (hovered_ != CaptionButton::None || pressed_ != CaptionButton::None) {
        hovered_ = CaptionButton::None;
        pressed_ = CaptionButton::None;
        invalidateButtons();
      }
      result = 0;
      return true;

    case WM_NCLBUTTONDOWN:
    case WM_NCLBUTTONDBLCLK: {
      const CaptionButton button = buttonForHitCode(wparam);
      // Anything else -- the caption's drag regions above all -- is Windows'
      // to handle: that is the move, and a double click's maximise.
      if (button == CaptionButton::None) {
        return false;
      }
      // Not passed on: DefWindowProc would run its own tracking loop for a
      // caption button and paint the classic one on top of ours.
      pressed_ = button;
      invalidateButtons();
      result = 0;
      return true;
    }

    case WM_NCLBUTTONUP: {
      const CaptionButton pressed = pressed_;
      if (pressed == CaptionButton::None) {
        return false;
      }
      pressed_ = CaptionButton::None;
      invalidateButtons();
      // A press only acts if it is released over the same button, as Windows'
      // own buttons behave.
      if (buttonForHitCode(wparam) == pressed) {
        WPARAM command = 0;
        switch (pressed) {
          case CaptionButton::Minimize:
            command = SC_MINIMIZE;
            break;
          case CaptionButton::Maximize:
            command = IsZoomed(window_) ? SC_RESTORE : SC_MAXIMIZE;
            break;
          case CaptionButton::Close:
            command = SC_CLOSE;
            break;
          case CaptionButton::None:
            break;
        }
        if (command != 0) {
          PostMessage(window_, WM_SYSCOMMAND, command, 0);
        }
      }
      result = 0;
      return true;
    }

    default:
      return false;
  }
}

// The four caption actions, reached from JavaScript rather than from a click.
//
// Each posts rather than sends. A caption action can destroy the window, and
// the caller here is a trampoline from the JavaScript thread: sending would
// run the close synchronously underneath a stack that still expects a window.
// Posting is also what the button path above does, so both arrive the same way
// and in the same order as everything else in the queue.
//
// Each tolerates no window. An app can ask before the host has one, and the
// honest answer to "minimise what?" is to do nothing rather than to crash.
void Win32TitleBar::minimize() {
  if (window_ == nullptr) {
    return;
  }
  PostMessage(window_, WM_SYSCOMMAND, SC_MINIMIZE, 0);
}

void Win32TitleBar::toggleMaximize() {
  if (window_ == nullptr) {
    return;
  }
  // Asked now rather than when the request was made: a window can be maximised
  // by a drag to the top edge in between, and then "toggle" means the opposite
  // of what it meant a moment ago.
  PostMessage(window_, WM_SYSCOMMAND, IsZoomed(window_) ? SC_RESTORE : SC_MAXIMIZE, 0);
}

void Win32TitleBar::close() {
  if (window_ == nullptr) {
    return;
  }
  // SC_CLOSE, not DestroyWindow: this way it arrives as WM_CLOSE, which is
  // where core's interceptClose gets its say. Closing any other way would let
  // an app refuse a close and then be unable to honour agreeing to one -- the
  // exact bug that not registering these left on this platform.
  PostMessage(window_, WM_SYSCOMMAND, SC_CLOSE, 0);
}

void Win32TitleBar::startDrag() {
  if (window_ == nullptr) {
    return;
  }
  // There is no "begin moving this window" call. Releasing the capture and
  // then posting a caption click hands Windows into its own move loop, which
  // is the same message its title bar sends itself. GetMessagePos is already
  // in the screen coordinates WM_NCLBUTTONDOWN wants.
  ReleaseCapture();
  PostMessage(window_, WM_NCLBUTTONDOWN, HTCAPTION, static_cast<LPARAM>(GetMessagePos()));
}

void Win32TitleBar::handleActivate(bool active) {
  if (active != active_) {
    active_ = active;
    invalidateButtons();
  }
}

void Win32TitleBar::clearHover() {
  if (hovered_ != CaptionButton::None && pressed_ == CaptionButton::None) {
    hovered_ = CaptionButton::None;
    invalidateButtons();
  }
}

void Win32TitleBar::invalidateButtons() {
  if (style_ != TitleBarStyle::Hidden || window_ == nullptr) {
    return;
  }
  RECT client{};
  GetClientRect(window_, &client);
  const auto caption = captionMetrics();
  RECT buttons{client.right - static_cast<LONG>(std::ceil(3 * caption.buttonWidth)),
               0,
               client.right,
               static_cast<LONG>(std::ceil(caption.height))};
  InvalidateRect(window_, &buttons, FALSE);
}

void Win32TitleBar::paintButtons(ID2D1RenderTarget *target) const {
  if (style_ != TitleBarStyle::Hidden || window_ == nullptr || target == nullptr) {
    return;
  }

  RECT client{};
  GetClientRect(window_, &client);
  const float width = static_cast<float>(client.right - client.left);
  const auto caption = captionMetrics();
  const float scale = static_cast<float>(dpiOf(window_)) / 96.0f;

  // Windows 11's own caption button colours: a faint wash of the glyph colour
  // on hover and press, and the close button's red.
  const D2D1_COLOR_F themeGlyph =
      dark_ ? D2D1::ColorF(1.0f, 1.0f, 1.0f) : D2D1::ColorF(0.0f, 0.0f, 0.0f);
  D2D1_COLOR_F glyph = textColor_.has_value() ? toColor(*textColor_) : themeGlyph;
  if (!active_) {
    glyph.a *= 0.45f;
  }
  const D2D1_COLOR_F closeRed = D2D1::ColorF(196.0f / 255.0f, 43.0f / 255.0f, 28.0f / 255.0f);

  target->SetTransform(D2D1::Matrix3x2F::Identity());

  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(glyph, &brush))) {
    return;
  }

  // Crisp one-device-pixel strokes at any scale: an odd width sits on a half
  // pixel, an even one on a whole one.
  const float stroke = std::max(1.0f, std::round(scale));
  const float offset = static_cast<int>(stroke) % 2 == 1 ? 0.5f : 0.0f;
  const float half = std::round(5.0f * scale);

  const CaptionButton order[3] = {
      CaptionButton::Minimize, CaptionButton::Maximize, CaptionButton::Close};
  for (int index = 0; index < 3; ++index) {
    const CaptionButton button = order[index];
    const float left = width - static_cast<float>(3 - index) * caption.buttonWidth;
    const D2D1_RECT_F cell{left, 0.0f, left + caption.buttonWidth, caption.height};

    const bool hovered = hovered_ == button;
    const bool pressed = pressed_ == button;
    D2D1_COLOR_F color = glyph;
    if (hovered || pressed) {
      D2D1_COLOR_F wash = themeGlyph;
      if (button == CaptionButton::Close) {
        wash = closeRed;
        wash.a = pressed ? 0.9f : 1.0f;
        color = D2D1::ColorF(1.0f, 1.0f, 1.0f);
      } else {
        wash.a = pressed ? 0.04f : 0.06f;
      }
      brush->SetColor(wash);
      target->FillRectangle(cell, brush.Get());
    }
    brush->SetColor(color);

    const float cx = std::floor(left + caption.buttonWidth / 2.0f) + offset;
    const float cy = std::floor(caption.height / 2.0f) + offset;

    switch (button) {
      case CaptionButton::Minimize:
        target->DrawLine(
            D2D1::Point2F(cx - half, cy), D2D1::Point2F(cx + half, cy), brush.Get(), stroke);
        break;
      case CaptionButton::Maximize:
        if (IsZoomed(window_)) {
          // Restore: a window in front, and the edge of one behind it.
          const float inset = std::round(2.0f * scale);
          target->DrawRectangle(
              D2D1::RectF(cx - half, cy - half + inset, cx + half - inset, cy + half),
              brush.Get(),
              stroke);
          target->DrawLine(D2D1::Point2F(cx - half + inset, cy - half),
                           D2D1::Point2F(cx + half, cy - half),
                           brush.Get(),
                           stroke);
          target->DrawLine(D2D1::Point2F(cx + half, cy - half),
                           D2D1::Point2F(cx + half, cy + half - inset),
                           brush.Get(),
                           stroke);
        } else {
          target->DrawRectangle(
              D2D1::RectF(cx - half, cy - half, cx + half, cy + half), brush.Get(), stroke);
        }
        break;
      case CaptionButton::Close:
        target->DrawLine(D2D1::Point2F(cx - half, cy - half),
                         D2D1::Point2F(cx + half, cy + half),
                         brush.Get(),
                         stroke);
        target->DrawLine(D2D1::Point2F(cx + half, cy - half),
                         D2D1::Point2F(cx - half, cy + half),
                         brush.Get(),
                         stroke);
        break;
      case CaptionButton::None:
        break;
    }
  }
}

} // namespace basalt
