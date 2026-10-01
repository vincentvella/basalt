// The window's title bar on Linux: its title, its colours, and whether GTK
// draws it or the app does.
//
// The same two styles Win32TitleBar.h and AppKitTitleBar.h describe, but GTK's
// window model is neither of theirs and the hidden style had to be built
// rather than switched on.
//
// - Native style. The window keeps its decorations. GTK draws them as part of
//   the window (client-side), but always *above* the child rather than over
//   it, so they cost the app nothing and every metric is zero -- the same
//   zeroes the other two report, arrived at differently. Colours are CSS on
//   the `headerbar` node, which is the only handle GTK offers.
//
// - Hidden style. `gtk_window_set_decorated(FALSE)` takes the whole titlebar
//   away, buttons included -- which is where GTK parts company with the other
//   two, whose systems keep drawing the caption buttons over the app's
//   content. So the host draws them: a GtkWindowControls in a GtkOverlay above
//   the surface root, which is the same picture by different means, and the
//   metrics describe it.
//
// GTK main thread only, like the window. The BasaltWindow module posts here.

#pragma once

#include <gtk/gtk.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace basalt {

enum class TitleBarStyle { Native, Hidden };

// What JavaScript is told, in logical pixels -- the surface root's units.
struct TitleBarMetrics {
  double height{0};
  double buttonsWidth{0};
  TitleBarStyle style{TitleBarStyle::Native};
};

class GtkTitleBar {
 public:
  // The window, and the controls the host overlays when the style is hidden.
  // Both borrowed; the host owns them for the life of the process.
  void attach(GtkWindow *window, GtkWidget *controls);

  void setTitle(std::optional<std::string> title);
  // 0xAARRGGBB as processColor produces, or nullopt for GTK's own.
  void setColors(std::optional<uint32_t> background,
                 std::optional<uint32_t> text,
                 std::optional<uint32_t> border);
  void setStyle(TitleBarStyle style);

  // What the caption buttons do, for an app that draws its own. The system's
  // buttons work without any of this; a header the app drew does not.
  void minimize();
  void toggleMaximize();
  void close();
  void startDrag();

  // Hands the window to the window manager's own move loop, from the press
  // that started it.
  //
  // `x` and `y` are in surface coordinates, and the device, button and
  // timestamp are the press's own: GDK will not begin a move without them,
  // which is the whole reason this takes arguments where startDrag() above
  // cannot. The host calls it from the gesture; see main_gtk.cpp.
  void beginMoveDrag(GdkDevice *device, int button, double x, double y, guint32 timestamp);

  TitleBarMetrics metrics() const;

  void setMetricsListener(std::function<void(const TitleBarMetrics &)> listener);

 private:
  void apply();
  void notify();

  GtkWindow *window_{nullptr};
  GtkWidget *controls_{nullptr};
  // The header bar this class installs, so there is something to style. See
  // attach().
  GtkWidget *header_{nullptr};
  GtkCssProvider *provider_{nullptr};
  std::optional<std::string> title_;
  std::optional<uint32_t> background_;
  std::optional<uint32_t> text_;
  std::optional<uint32_t> border_;
  TitleBarStyle style_{TitleBarStyle::Native};
  std::function<void(const TitleBarMetrics &)> listener_;
};

GtkTitleBar &titleBar();

} // namespace basalt
