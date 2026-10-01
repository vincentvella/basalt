// Exercises the GTK view layer with hand-written frames, standing in for what
// Fabric would emit. Builds and runs without the React Native C++ core, so the
// paint and layout path can be verified on its own.

#include "RnView.h"

static GdkRGBA rgba(float r, float g, float b, float a) {
  GdkRGBA c;
  c.red = r; c.green = g; c.blue = b; c.alpha = a;
  return c;
}

static RnView *make_box(int tag, float x, float y, float w, float h, GdkRGBA color) {
  RnView *view = rn_view_new(tag);
  rn_view_set_frame(view, x, y, w, h);
  rn_view_set_background_color(view, TRUE, &color);
  return view;
}

static void on_activate(GtkApplication *app, gpointer /*user_data*/) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "basalt-core — mounting skeleton");
  gtk_window_set_default_size(GTK_WINDOW(window), 640, 420);

  // Root surface.
  RnView *root = make_box(1, 0, 0, 640, 420, rgba(0.12f, 0.13f, 0.16f, 1.0f));

  // A row of children, absolutely placed the way Yoga would have resolved them.
  RnView *a = make_box(2, 32, 32, 240, 160, rgba(0.30f, 0.55f, 0.95f, 1.0f));
  RnView *b = make_box(3, 296, 32, 240, 160, rgba(0.95f, 0.45f, 0.35f, 1.0f));
  RnView *c = make_box(4, 32, 224, 504, 140, rgba(0.35f, 0.80f, 0.55f, 1.0f));

  rn_view_insert_child(root, a, 0);
  rn_view_insert_child(root, b, 1);
  rn_view_insert_child(root, c, 2);

  // Nested child, to prove coordinates are parent-relative.
  RnView *nested = make_box(5, 24, 24, 120, 90, rgba(1.0f, 1.0f, 1.0f, 0.85f));
  rn_view_insert_child(a, nested, 0);

  // Half-opacity child, to prove the opacity layer.
  RnView *faded = make_box(6, 24, 24, 160, 90, rgba(0.1f, 0.1f, 0.1f, 1.0f));
  rn_view_set_opacity(faded, 0.4);
  rn_view_insert_child(c, faded, 0);

  gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(root));
  gtk_window_present(GTK_WINDOW(window));
}

int main(int argc, char **argv) {
  GtkApplication *app =
      gtk_application_new("dev.basalt.skeleton", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
  const int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
