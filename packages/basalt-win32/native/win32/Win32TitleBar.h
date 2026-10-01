// The window's title bar on Windows: its title, its colours, dark or light,
// and -- when an app asks for it hidden -- the caption the host draws instead.
//
// Two different things share this class, because they share one window and
// one set of requests from JavaScript:
//
// - Native style. Windows draws the caption, and this only talks to DWM. Dark
//   mode is DWMWA_USE_IMMERSIVE_DARK_MODE, following the app's effective colour
//   scheme. Colours are DWMWA_CAPTION_COLOR, DWMWA_TEXT_COLOR and
//   DWMWA_BORDER_COLOR, which Windows 11 honours and Windows 10 ignores.
//
// - Hidden style. WM_NCCALCSIZE gives the caption's height back to the client
//   area, so the app's content reaches the top of the window. The resize edge
//   along the top, the three caption buttons and the app's drag regions are then
//   this class's to answer WM_NCHITTEST for, and the buttons are painted by the
//   host over the app's content. Answering HTMAXBUTTON over the maximise button
//   is also what makes Windows 11 offer its snap layouts there.
//
// UI thread only, like the window, except where a method says otherwise. The
// BasaltWindow module posts its requests here.

#pragma once

#include "Win32TitleBarLayout.h"

#include <windows.h>

#include <functional>
#include <mutex>
#include <optional>
#include <string>

struct ID2D1RenderTarget;

namespace basalt {

namespace win32 {
class RnWin32View;
} // namespace win32

enum class TitleBarStyle { Native, Hidden };

// What JavaScript is told about the caption, in client pixels -- which are the
// surface root's units on this host. All zero while the style is native: the
// system's caption is outside the client area and takes nothing from the app.
struct TitleBarMetrics {
  float height{0};
  float buttonsWidth{0};
  TitleBarStyle style{TitleBarStyle::Native};
};

class Win32TitleBar {
 public:
  using MetricsListener = std::function<void(const TitleBarMetrics &)>;

  void attach(HWND window);

  // The title shown when the app has not set one: its name, which the host
  // decides.
  void setDefaultTitle(const std::string &title);
  // The app's title, or nullopt to go back to the default.
  void setTitle(std::optional<std::string> title);

  void setDarkMode(bool dark);

  // Each a COLORREF, or nullopt for the system's own.
  void setColors(std::optional<COLORREF> caption,
                 std::optional<COLORREF> text,
                 std::optional<COLORREF> border);

  void setStyle(TitleBarStyle style);
  TitleBarStyle style() const { return style_; }

  // Safe from any thread: the module reads it on the JavaScript thread.
  TitleBarMetrics metrics() const;
  // The one listener, or null to clear it. Safe from any thread; called on the
  // UI thread whenever the metrics change.
  void setMetricsListener(MetricsListener listener);
  // After a resize or a DPI change, either of which can move the caption
  // without anything having been asked.
  void refreshMetrics();

  // The caption's four actions, which an app-drawn header asks for by name.
  // GtkWindowModule and AppKitWindowModule register the same four, and this
  // host registered none of them: an app could draw its own header on Windows
  // and then find its close button did nothing.
  //
  // They are what the host-drawn buttons already do on a click -- see the
  // WM_NCLBUTTONUP case in handleNcMouse, which posts the same WM_SYSCOMMAND.
  // Naming them here is what lets JavaScript ask for the same thing, and is
  // what stops the two paths drifting apart.
  //
  // Each is safe with no window attached, which is what an app asking before
  // the host has a window would do.
  void minimize();
  void toggleMaximize();
  void close();
  // Hands the drag to Windows, which runs the move loop itself. There is no
  // "start moving this window" call: the idiom is to release the capture and
  // then post a caption click, which is what the system's own title bar does.
  void startDrag();

  // Window procedure hooks. Each returns true when it answered the message,
  // with the answer in `result`; false means the host should carry on as it
  // would have.
  bool handleNcCalcSize(WPARAM wparam, LPARAM lparam, LRESULT &result);
  bool handleNcHitTest(LPARAM lparam, win32::RnWin32View *root, LRESULT &result);
  bool handleNcMouse(UINT message, WPARAM wparam, LRESULT &result);
  void handleActivate(bool active);
  // The pointer is over the client area, so no caption button is hovered.
  void clearHover();

  // Paints the three caption buttons over whatever has already been drawn, in
  // client coordinates. Does nothing unless the style is hidden.
  void paintButtons(ID2D1RenderTarget *target) const;

 private:
  win32::CaptionMetrics captionMetrics() const;
  void applyTitle();
  void applyColors();
  void redrawFrame();
  void invalidateButtons();
  void publishMetrics();

  HWND window_{nullptr};
  TitleBarStyle style_{TitleBarStyle::Native};
  bool dark_{false};
  bool active_{true};
  std::string defaultTitle_;
  std::optional<std::string> title_;
  std::optional<COLORREF> captionColor_;
  std::optional<COLORREF> textColor_;
  std::optional<COLORREF> borderColor_;
  win32::CaptionButton hovered_{win32::CaptionButton::None};
  win32::CaptionButton pressed_{win32::CaptionButton::None};
  bool trackingLeave_{false};

  mutable std::mutex mutex_;
  TitleBarMetrics metrics_;
  MetricsListener listener_;
};

// The host's one title bar.
Win32TitleBar &titleBar();

} // namespace basalt
