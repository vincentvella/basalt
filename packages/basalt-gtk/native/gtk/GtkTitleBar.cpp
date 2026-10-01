#include "GtkTitleBar.h"

#include <cstdio>

namespace basalt {

namespace {

// processColor's 0xAARRGGBB as a CSS colour. Alpha included: a header bar can
// be translucent where a Windows caption cannot.
std::string cssColor(uint32_t argb) {
  char buffer[64];
  std::snprintf(buffer,
                sizeof(buffer),
                "rgba(%u,%u,%u,%.3f)",
                (argb >> 16) & 0xFFu,
                (argb >> 8) & 0xFFu,
                argb & 0xFFu,
                static_cast<double>((argb >> 24) & 0xFFu) / 255.0);
  return buffer;
}

} // namespace

void GtkTitleBar::attach(GtkWindow *window, GtkWidget *controls) {
  // Weak, so these become NULL when the widgets go rather than dangling.
  //
  // This outlives the window: it is a process-wide singleton and the window is
  // destroyed at quit, or by close() below, or by the user. Every method here
  // is reachable from JavaScript, which is still running at that point -- so a
  // raw pointer is a use-after-free waiting for a late setTitle. AppKit's half
  // gets this for free from __weak; GTK has to ask.
  if (window_ != nullptr) {
    g_object_remove_weak_pointer(G_OBJECT(window_), reinterpret_cast<gpointer *>(&window_));
  }
  if (controls_ != nullptr) {
    g_object_remove_weak_pointer(G_OBJECT(controls_), reinterpret_cast<gpointer *>(&controls_));
  }

  window_ = window;
  controls_ = controls;
  header_ = nullptr;

  if (window_ != nullptr) {
    g_object_add_weak_pointer(G_OBJECT(window_), reinterpret_cast<gpointer *>(&window_));
  }
  if (controls_ != nullptr) {
    g_object_add_weak_pointer(G_OBJECT(controls_), reinterpret_cast<gpointer *>(&controls_));
  }

  apply();
}

void GtkTitleBar::setTitle(std::optional<std::string> title) {
  title_ = std::move(title);
  apply();
}

void GtkTitleBar::setColors(std::optional<uint32_t> background,
                            std::optional<uint32_t> text,
                            std::optional<uint32_t> border) {
  background_ = background;
  text_ = text;
  border_ = border;
  apply();
}

void GtkTitleBar::setStyle(TitleBarStyle style) {
  if (style_ == style) {
    return;
  }
  style_ = style;
  apply();
  notify();
}

void GtkTitleBar::minimize() {
  if (window_ != nullptr) {
    gtk_window_minimize(window_);
  }
}

void GtkTitleBar::toggleMaximize() {
  if (window_ == nullptr) {
    return;
  }
  // GTK has a maximised state to read, unlike AppKit's zoom, so this really is
  // a toggle rather than a request to swap frames.
  if (gtk_window_is_maximized(window_)) {
    gtk_window_unmaximize(window_);
  } else {
    gtk_window_maximize(window_);
  }
}

void GtkTitleBar::close() {
  if (window_ != nullptr) {
    // close() rather than destroy(): it asks, so a close-request handler still
    // gets its say, which is how the real button behaves.
    gtk_window_close(window_);
  }
}

void GtkTitleBar::startDrag() {
  // Still nothing to do here, but not for the reason this used to give.
  //
  // It said there is no "begin a drag now" call. There is:
  // gdk_toplevel_begin_move, which beginMoveDrag below uses. What there is no
  // way to supply is its arguments -- the device, the button and the
  // timestamp of the press GDK is being asked to take over. By the time
  // JavaScript can call this, the press has been delivered, handled and
  // forgotten, and a move begun without it is refused by the compositor or
  // starts a drag the pointer is not in.
  //
  // So the conclusion the old comment reached was right even though its
  // premise was wrong: this belongs in the host, at the press. That is where
  // it now is -- main_gtk.cpp hit-tests the drag regions on a click gesture
  // and calls beginMoveDrag. An app marking <TitleBar.DragRegion> needs
  // nothing from this method.
}

void GtkTitleBar::beginMoveDrag(GdkDevice *device,
                                int button,
                                double x,
                                double y,
                                guint32 timestamp) {
  // window_ is a weak pointer and the device comes from an event that has
  // already been dispatched, so both can be gone by the time this runs.
  if (window_ == nullptr || device == nullptr) {
    return;
  }

  GdkSurface *const surface = gtk_native_get_surface(GTK_NATIVE(window_));
  // Not every GdkSurface is a toplevel -- a popup is not -- and only a
  // toplevel can be moved. A window that has not been mapped yet has no
  // surface at all, which is what an app calling this during its first render
  // would reach.
  if (surface == nullptr || !GDK_IS_TOPLEVEL(surface)) {
    return;
  }

  gdk_toplevel_begin_move(GDK_TOPLEVEL(surface), device, button, x, y, timestamp);
}

TitleBarMetrics GtkTitleBar::metrics() const {
  TitleBarMetrics result;
  result.style = style_;
  if (style_ == TitleBarStyle::Native || controls_ == nullptr) {
    // GTK's own decorations sit above the child rather than over it, so they
    // take nothing from the app and there is nothing to report.
    return result;
  }

  // Measured rather than assumed: how tall the buttons are and how much room
  // they need is the theme's business, and a hard-coded 32 would be wrong on
  // every desktop that disagrees.
  int minimum = 0;
  int natural = 0;
  gtk_widget_measure(controls_, GTK_ORIENTATION_HORIZONTAL, -1, &minimum, &natural, nullptr, nullptr);
  result.buttonsWidth = natural;
  gtk_widget_measure(controls_, GTK_ORIENTATION_VERTICAL, -1, &minimum, &natural, nullptr, nullptr);
  result.height = natural;
  return result;
}

void GtkTitleBar::setMetricsListener(std::function<void(const TitleBarMetrics &)> listener) {
  listener_ = std::move(listener);
}

void GtkTitleBar::apply() {
  if (window_ == nullptr) {
    return;
  }

  if (title_.has_value()) {
    gtk_window_set_title(window_, title_->c_str());
  }

  const bool hidden = style_ == TitleBarStyle::Hidden;
  // The header bar goes away with the decorations rather than being left
  // behind empty, which is what `set_decorated(FALSE)` alone would do on a
  // window that has one.
  if (header_ != nullptr) {
    gtk_widget_set_visible(header_, hidden ? FALSE : TRUE);
  }
  gtk_window_set_decorated(window_, hidden ? FALSE : TRUE);
  if (controls_ != nullptr) {
    // The host's stand-in for the caption buttons the other two systems keep
    // drawing. Only when there are no real ones.
    gtk_widget_set_visible(controls_, hidden ? TRUE : FALSE);
  }

  // A header bar of our own, and only once an app has asked for a colour.
  //
  // Without one there is nothing to style: GTK draws a `headerbar` node only
  // when it is doing client-side decorations, and where it is not -- an X11
  // session with server-side decorations, or the quartz backend, where macOS
  // draws the frame -- the caption belongs to something CSS cannot reach.
  //
  // Installed lazily rather than at startup because `set_titlebar` takes
  // vertical space from the child. A window that was never asked about its
  // title bar must be exactly the window it was before this class existed --
  // the first version installed one unconditionally and moved every app's
  // layout down by the height of a header, which two end-to-end scenarios
  // noticed by tapping where a button no longer was.
  // Worth knowing what this costs: a GtkHeaderBar draws GTK's own window
  // controls, so from here the buttons are Adwaita's rather than whatever the
  // window frame had. On the target desktop that is the same thing -- GNOME
  // does client-side decorations, and these *are* the system's buttons. On a
  // backend where something else draws the frame, the quartz one used for
  // development among them, it is visibly GTK's buttons on someone else's
  // window. Unavoidable: CSS is the only handle GTK offers for a caption
  // colour, and it reaches nothing the system drew.
  const bool wantsColour = background_.has_value() || text_.has_value() || border_.has_value();
  if (wantsColour && header_ == nullptr) {
    header_ = gtk_header_bar_new();
    gtk_header_bar_set_show_title_buttons(GTK_HEADER_BAR(header_), TRUE);
    gtk_window_set_titlebar(window_, header_);
    g_object_add_weak_pointer(G_OBJECT(header_), reinterpret_cast<gpointer *>(&header_));
    gtk_widget_set_visible(header_, hidden ? FALSE : TRUE);
  }

  // Colours, as CSS on the header bar. There is no property for any of this:
  // a GtkWindow's decorations are styled or they are the theme's.
  if (provider_ == nullptr) {
    GdkDisplay *display = gtk_widget_get_display(GTK_WIDGET(window_));
    if (display == nullptr) {
      // No display yet, so nothing to style and nothing drawn. The next call
      // installs it.
      return;
    }
    provider_ = gtk_css_provider_new();
    gtk_style_context_add_provider_for_display(
        display, GTK_STYLE_PROVIDER(provider_), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }

  std::string css;
  if (background_.has_value() || text_.has_value() || border_.has_value()) {
    css += "headerbar, headerbar > windowhandle {";
    if (background_.has_value()) {
      // `background-image: none` as well: the theme's header bar is usually a
      // gradient, and a background-color alone leaves it on top.
      css += "background-image: none; background-color: " + cssColor(*background_) + ";";
    }
    if (text_.has_value()) {
      css += "color: " + cssColor(*text_) + ";";
    }
    if (border_.has_value()) {
      // Unlike AppKit, GTK does have somewhere to put this.
      css += "border-bottom: 1px solid " + cssColor(*border_) + ";";
    }
    css += "}";
    // The title label inherits colour from the header bar on most themes and
    // not on all, so it is named too.
    if (text_.has_value()) {
      css += "headerbar label { color: " + cssColor(*text_) + "; }";
    }
  }
  gtk_css_provider_load_from_string(provider_, css.c_str());
}

void GtkTitleBar::notify() {
  if (listener_) {
    listener_(metrics());
  }
}

GtkTitleBar &titleBar() {
  static GtkTitleBar instance;
  return instance;
}

} // namespace basalt
