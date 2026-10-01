// The Linux half of core/WindowControl.h.
//
// GTK4 is the most opinionated of the three desktops about this, and several of
// these functions are shaped by what it will not do. `windowCapabilities()` is
// how an app finds that out without reading this file.
//
// **A window cannot be positioned.** `gtk_window_move` is gone in GTK4 and
// nothing replaced it: on Wayland a client has no idea where its window is and
// no way to say, and GTK removed the X11 call rather than ship an API that
// works on one display server. So `setWindowPosition` and the `x`/`y` in
// `windowBounds` are honest zeroes, and `centerWindow` is the window manager's
// job -- which, on both display servers, it does by default for a window that
// has not been placed.
//
// **A window's size is a request.** `gtk_window_set_default_size` is what a
// client has; the compositor may ignore it. That is also true of the other two
// desktops in a tiling window manager, and the answer is the same: ask, and
// report what actually happened through the bounds listener.
//
// **A window has no maximum size and does not float.**
// `gtk_window_set_geometry_hints` and `gtk_window_set_keep_above` were both
// removed in GTK4, for the same reason as `gtk_window_move`: Wayland has no
// protocol for either, and an API that worked on X11 alone would be one that
// silently does nothing half the time -- which is the thing GTK4 decided not to
// ship. A minimum *is* expressible, because it is a property of the widget
// rather than a request to the compositor.

#include <vector>
#include "WindowControl.h"

#include "GtkTitleBar.h"

#include <gtk/gtk.h>

namespace basalt {

namespace {


// The window this process owns. The same question PlatformServicesGtk.cpp asks
// for a popup's parent, and asked the same way: GTK has a list of toplevels
// rather than an idea of "the app's window".
GtkWindow *window() {
  GListModel *toplevels = gtk_window_get_toplevels();
  if (toplevels == nullptr) {
    return nullptr;
  }
  GtkWindow *fallback = nullptr;
  const guint count = g_list_model_get_n_items(toplevels);
  for (guint i = 0; i < count; i++) {
    auto *candidate = static_cast<GtkWindow *>(g_list_model_get_item(toplevels, i));
    if (candidate == nullptr) {
      continue;
    }
    const bool visible = gtk_widget_get_visible(GTK_WIDGET(candidate)) != FALSE;
    if (visible && gtk_window_is_active(candidate)) {
      g_object_unref(candidate);
      return candidate;
    }
    if (visible && fallback == nullptr) {
      fallback = candidate;
    }
    g_object_unref(candidate);
  }
  return fallback;
}

} // namespace

// The displays, and the three things GTK 4 will not say about them.
//
// `gdk_monitor_get_workarea`, `gdk_display_get_primary_monitor` and any way to
// ask where the pointer is were all in GTK 3 and are all gone -- not
// overlooked, removed. Wayland has no protocol for a work area a client can
// read, no notion of a primary output, and tells a client the pointer's
// position only while it is over one of that client's surfaces. GTK 4 dropped
// the X11-only versions rather than offer an API that answers on one display
// server and not the other.
//
// So the work area is reported as the full bounds, the first monitor is
// reported as primary, and the pointer is reported as unknown. Each is a
// truthful answer to a question this desktop does not answer, which is the
// same rule `WindowCapabilities` follows for position and always-on-top.
std::vector<DisplayInfo> displays() {
  std::vector<DisplayInfo> found;
  GdkDisplay *display = gdk_display_get_default();
  if (display == nullptr) {
    return found;
  }
  GListModel *monitors = gdk_display_get_monitors(display);
  if (monitors == nullptr) {
    return found;
  }
  const guint count = g_list_model_get_n_items(monitors);
  for (guint i = 0; i < count; i++) {
    auto *monitor = static_cast<GdkMonitor *>(g_list_model_get_item(monitors, i));
    if (monitor == nullptr) {
      continue;
    }
    if (gdk_monitor_is_valid(monitor)) {
      GdkRectangle geometry{};
      gdk_monitor_get_geometry(monitor, &geometry);

      DisplayInfo info;
      info.x = geometry.x;
      info.y = geometry.y;
      info.width = geometry.width;
      info.height = geometry.height;
      // No work area to ask for; see above.
      info.workX = info.x;
      info.workY = info.y;
      info.workWidth = info.width;
      info.workHeight = info.height;
      // `gdk_monitor_get_scale`, not `get_scale_factor`: the factor is an
      // integer and a display at 150% is real, which is the case the integer
      // rounds away.
      const double scale = gdk_monitor_get_scale(monitor);
      info.scaleFactor = scale > 0.0 ? scale : 1.0;
      info.primary = found.empty();
      found.push_back(info);
    }
    g_object_unref(monitor);
  }
  return found;
}

PointerPosition pointerPosition() {
  // Unknown, and not a gap this host can fill: see above.
  return {};
}

WindowBounds windowBounds() {
  WindowBounds bounds;
  GtkWindow *target = window();
  if (target == nullptr) {
    return bounds;
  }
  bounds.width = gtk_widget_get_width(GTK_WIDGET(target));
  bounds.height = gtk_widget_get_height(GTK_WIDGET(target));
  bounds.fullScreen = gtk_window_is_fullscreen(target) != FALSE;
  bounds.maximized = gtk_window_is_maximized(target) != FALSE;
  // x and y stay zero. See the header of this file: GTK4 has no way to ask.
  return bounds;
}

void setWindowSize(double width, double height) {
  GtkWindow *target = window();
  if (target == nullptr || width <= 0.0 || height <= 0.0) {
    return;
  }
  // `default_size` rather than a resize call, because that is the only one GTK4
  // has: it is the size the window takes when it is not being managed into
  // something else, and setting it on a mapped window resizes it.
  gtk_window_set_default_size(target, static_cast<int>(width), static_cast<int>(height));
}

void setWindowPosition(double /*x*/, double /*y*/) {
  // Deliberately nothing. See the header: GTK4 removed `gtk_window_move` and
  // Wayland has no equivalent, so an app asking for a position is asking for
  // something no Linux desktop will do. Failing silently is the same answer the
  // compositor gives and is better than a warning an app cannot act on.
}

void centerWindow() {
  // Also nothing, and for the same reason -- centring is positioning. Both
  // display servers centre an unplaced window by default, so an app that never
  // moves its window already gets what this would have asked for.
}

void setWindowFullScreen(bool fullScreen) {
  GtkWindow *target = window();
  if (target == nullptr) {
    return;
  }
  if (fullScreen) {
    gtk_window_fullscreen(target);
  } else {
    gtk_window_unfullscreen(target);
  }
}


void applyWindowSizeLimits() {
  GtkWindow *target = window();
  if (target == nullptr) {
    return;
  }
  // The minimum only, and through the widget rather than the window manager:
  // GTK works out a toplevel's minimum from what its content needs, and this is
  // how a client adds to that. -1 is GTK's "no request", which is what a
  // cleared limit means.
  const WindowSizeLimits limits = windowSizeLimits();
  gtk_widget_set_size_request(GTK_WIDGET(target),
                              limits.minWidth > 0.0 ? static_cast<int>(limits.minWidth) : -1,
                              limits.minHeight > 0.0 ? static_cast<int>(limits.minHeight) : -1);
  // The maximum is dropped rather than approximated. A window that enforced one
  // by resizing itself back down would fight the person dragging its corner,
  // which is worse than not having the feature; `windowCapabilities()` says so
  // instead.
}

void setWindowResizable(bool resizable) {
  GtkWindow *target = window();
  if (target != nullptr) {
    gtk_window_set_resizable(target, resizable ? TRUE : FALSE);
  }
}

void setWindowAlwaysOnTop(bool /*alwaysOnTop*/) {
  // Nothing to do, and that is the answer rather than a gap. See the header.
}

WindowCapabilities windowCapabilities() {
  WindowCapabilities capabilities;
  capabilities.position = false;
  capabilities.minimumSize = true;
  capabilities.maximumSize = false;
  capabilities.resizable = true;
  capabilities.alwaysOnTop = false;
  return capabilities;
}

} // namespace basalt
