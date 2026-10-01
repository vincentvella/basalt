// The title bar's metrics and what it does to the window.
//
// What the two styles look like is in the screenshots that went with phase 58;
// what is assertable is the arithmetic an app lays itself out against, and
// whether the window really took the style rather than the metrics being
// computed from a flag nothing acted on.

#include "TestHarness.h"

#include "GtkTitleBar.h"

#include <sstream>

namespace {

// A window and the controls the host overlays, built the way main_gtk.cpp
// builds them. Never shown: the metrics are a property of the widgets rather
// than of anything on screen.
struct Fixture {
  GtkWindow *window;
  GtkWidget *controls;

  Fixture() {
    window = GTK_WINDOW(gtk_window_new());
    controls = gtk_window_controls_new(GTK_PACK_END);
    GtkWidget *overlay = gtk_overlay_new();
    gtk_overlay_add_overlay(GTK_OVERLAY(overlay), controls);
    gtk_window_set_child(window, overlay);
    basalt::titleBar().attach(window, controls);
  }

  ~Fixture() { gtk_window_destroy(window); }
};

} // namespace

TEST(titlebar_native_style_costs_the_app_nothing) {
  Fixture fixture;
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);

  // GTK's decorations sit above the child rather than over it, so they take
  // nothing from the app. Windows and macOS report the same zeroes, arrived at
  // differently -- there the system's caption is outside the client area.
  const auto metrics = basalt::titleBar().metrics();
  EXPECT(metrics.style == basalt::TitleBarStyle::Native);
  EXPECT_EQ(metrics.height, 0.0);
  EXPECT_EQ(metrics.buttonsWidth, 0.0);
  EXPECT(gtk_window_get_decorated(fixture.window));
}

TEST(titlebar_hidden_style_reports_what_to_leave_clear) {
  Fixture fixture;
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Hidden);

  const auto metrics = basalt::titleBar().metrics();
  EXPECT(metrics.style == basalt::TitleBarStyle::Hidden);
  // Measured from the controls rather than assumed: how big they are is the
  // theme's business, and a hard-coded size would be wrong on every desktop
  // that disagrees.
  EXPECT(metrics.height > 0.0);
  EXPECT(metrics.buttonsWidth > 0.0);

  // The window really did take the style.
  EXPECT(!gtk_window_get_decorated(fixture.window));
  // And the host's stand-in buttons are up, because GTK's own went with the
  // decorations -- which is where this host differs from the other two.
  EXPECT(gtk_widget_get_visible(fixture.controls));

  // Back again, so an unmounted request restores the window.
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);
  EXPECT(gtk_window_get_decorated(fixture.window));
  EXPECT(!gtk_widget_get_visible(fixture.controls));
  EXPECT_EQ(basalt::titleBar().metrics().height, 0.0);
}

TEST(titlebar_takes_a_title) {
  Fixture fixture;
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);
  basalt::titleBar().setTitle(std::string("Inbox"));

  const char *title = gtk_window_get_title(fixture.window);
  EXPECT_EQ(std::string(title != nullptr ? title : ""), std::string("Inbox"));
}

// The caption buttons' functions, for an app drawing its own header.
//
// What these *do* was checked live -- a probe drove the window from 900x700 to
// maximised and back on both hosts, then minimised and closed it. What a test
// can pin is the half that has no window on the other end: the module outlives
// the window at shutdown, and a JavaScript call arriving then must not take the
// process with it.
TEST(titlebar_actions_are_safe_with_no_window) {
  basalt::titleBar().attach(nullptr, nullptr);
  basalt::titleBar().minimize();
  basalt::titleBar().toggleMaximize();
  basalt::titleBar().close();
  basalt::titleBar().startDrag();
  EXPECT(true);
}

TEST(titlebar_toggles_maximize) {
  Fixture fixture;
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Native);

  // An unmapped window still records the request, which is what this asks
  // about -- whether the toggle reads the state and flips it rather than
  // always maximising.
  basalt::titleBar().toggleMaximize();
  EXPECT(gtk_window_is_maximized(fixture.window));
  basalt::titleBar().toggleMaximize();
  EXPECT(!gtk_window_is_maximized(fixture.window));
}

// The window going away underneath it, which is what happens at quit and what
// Window.close() does on purpose -- with JavaScript still running and still
// able to call any of this.
TEST(titlebar_survives_its_window_being_destroyed) {
  GtkWindow *window = GTK_WINDOW(gtk_window_new());
  GtkWidget *controls = gtk_window_controls_new(GTK_PACK_END);
  GtkWidget *overlay = gtk_overlay_new();
  gtk_overlay_add_overlay(GTK_OVERLAY(overlay), controls);
  gtk_window_set_child(window, overlay);

  basalt::titleBar().attach(window, controls);
  basalt::titleBar().setStyle(basalt::TitleBarStyle::Hidden);

  gtk_window_destroy(window);

  // Every one of these is reachable from JavaScript after the window has gone.
  basalt::titleBar().setTitle(std::string("late"));
  basalt::titleBar().setColors(0xFF4285F4u, std::nullopt, std::nullopt);
  basalt::titleBar().minimize();
  basalt::titleBar().toggleMaximize();
  basalt::titleBar().close();
  const auto metrics = basalt::titleBar().metrics();
  EXPECT_EQ(metrics.height, 0.0);
}
