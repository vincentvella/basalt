// Linux's half of the colour-scheme seam.
//
// `GtkSettings:gtk-application-prefer-dark-theme` is what a GTK application
// reads, and what a desktop environment sets when the user asks for dark mode.
//
// The more modern answer is the `org.freedesktop.appearance color-scheme`
// setting from the XDG desktop portal, which works across toolkits and inside a
// Flatpak sandbox. It needs a D-Bus round trip and a subscription, and GTK
// already reflects the portal's value into this setting on a desktop that has
// one -- so this is the same answer with none of the plumbing. Worth revisiting
// if a sandboxed build ever disagrees with the desktop around it.

#include "ColorScheme.h"

#include <gtk/gtk.h>

namespace basalt {

namespace {

void onPreferDarkChanged(GObject * /*settings*/, GParamSpec * /*spec*/, gpointer /*userData*/) {
  notifyColorSchemeChanged();
}

bool gObserving = false;

} // namespace

ColorScheme systemColorScheme() {
  // gtk_settings_get_default returns null before gtk_init, which is where a
  // unit test reads from. Light is React Native's default when a platform has
  // no answer.
  GtkSettings *settings = gtk_settings_get_default();
  if (settings == nullptr) {
    return ColorScheme::Light;
  }

  gboolean prefersDark = FALSE;
  g_object_get(settings, "gtk-application-prefer-dark-theme", &prefersDark, nullptr);
  return prefersDark == TRUE ? ColorScheme::Dark : ColorScheme::Light;
}

void startObservingColorScheme() {
  if (gObserving) {
    return;
  }
  GtkSettings *settings = gtk_settings_get_default();
  if (settings == nullptr) {
    return;
  }
  g_signal_connect(settings,
                   "notify::gtk-application-prefer-dark-theme",
                   G_CALLBACK(onPreferDarkChanged),
                   nullptr);
  gObserving = true;
}

} // namespace basalt
