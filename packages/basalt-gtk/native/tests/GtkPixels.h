// Rendering a widget's own snapshot to pixels, with no window and no display.
//
// `docs/backlog/testing.md` carried "no rendering assertions on GTK" from the
// start, on the understanding that pixels here would need a display server and a
// window that had been shown. They do not: `gsk_renderer_realize` takes a NULL
// surface, so a node tree can be rasterised into a `GdkTexture` and downloaded,
// which is a dozen lines and no permissions.
//
// That matters because the tree dump says a view *has* a colour, a frame and a
// transform, not that the right pixels reached the screen -- and the bug that
// entry was written about was exactly that gap: GTK's cairo renderer mangled
// every transform in the demo and no test noticed.
//
// ## Which renderer
//
// The GL one, which is what a real application uses, falling back to cairo on a
// machine with no GL. `rendererName` says which answered, because the two are
// not interchangeable for every question: the cairo renderer draws a transformed
// widget subtree unrotated, which is recorded in backlog/upstream.md, so a test
// about transforms has to know which one it is looking at. Colours, clips,
// borders and opacity are the same in both.

#pragma once

#include "RnView.h"

#include <gtk/gtk.h>

#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace basalt::testing {

struct RnPixel {
  int red;
  int green;
  int blue;
  int alpha;
};

// One rendered image, with straight (unpremultiplied) components so that a
// half-transparent red reads as red at half alpha rather than as a darker red.
class RnPixels {
 public:
  RnPixels(int width, int height, std::vector<guchar> bytes, std::string renderer)
      : width_(width), height_(height), bytes_(std::move(bytes)), renderer_(std::move(renderer)) {}

  RnPixel at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || bytes_.empty()) {
      return RnPixel{-1, -1, -1, -1};
    }
    const std::size_t stride = static_cast<std::size_t>(width_) * 4;
    const std::size_t at = static_cast<std::size_t>(y) * stride + static_cast<std::size_t>(x) * 4;
    // `gdk_texture_download` writes premultiplied BGRA in native byte order,
    // which is little-endian on everything this runs on.
    const int alpha = bytes_[at + 3];
    const auto straight = [alpha](int value) {
      return alpha == 0 ? 0 : static_cast<int>(value * 255.0 / alpha + 0.5);
    };
    return RnPixel{straight(bytes_[at + 2]), straight(bytes_[at + 1]), straight(bytes_[at + 0]),
                   alpha};
  }

  bool valid() const { return !bytes_.empty(); }
  // "GskGLRenderer" or "GskCairoRenderer"; empty when neither would realize.
  const std::string &rendererName() const { return renderer_; }

 private:
  int width_;
  int height_;
  std::vector<guchar> bytes_;
  std::string renderer_;
};

// Spins the main loop until `ready` answers, or a deadline passes. Bounded, so a
// machine that never produces a frame fails an assertion rather than hanging.
inline void pumpUntil(const std::function<bool()> &ready) {
  const gint64 deadline = g_get_monotonic_time() + 2000000; // 2s
  while (g_get_monotonic_time() < deadline) {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
    if (ready()) {
      return;
    }
    g_usleep(1000);
  }
}

// Snapshots a tree the way GTK would and rasterises the result.
//
// **A window, and why.** A single view with a background needs nothing: allocate
// it and its own snapshot has its colour in it. A view with *children* needs the
// tree mapped, because `gtk_widget_snapshot_child` draws nothing for an unmapped
// child -- which is the same reason tests/test_hittest.cpp shows a window before
// picking. So this puts the tree in a window, shows it, and waits for the
// allocation, exactly as those tests do. The window is never ordered front and
// is destroyed on the way out.
//
// The snapshot itself goes through the class vfunc rather than
// `gtk_widget_snapshot`, which is what the node-counting tests do: the vfunc is
// the thing under test, and going through it keeps the node tree and the pixels
// answering the same question.
inline RnPixels renderView(RnView *view, int width, int height) {
  GtkWidget *window = nullptr;
  if (gtk_widget_get_parent(GTK_WIDGET(view)) == nullptr) {
    window = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(view));
    gtk_widget_set_visible(window, TRUE);
    pumpUntil([view]() {
      GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(view));
      return gtk_widget_get_mapped(GTK_WIDGET(view)) &&
             (child == nullptr || gtk_widget_get_width(child) > 0);
    });
  }
  gtk_widget_set_size_request(GTK_WIDGET(view), width, height);
  gtk_widget_allocate(GTK_WIDGET(view), width, height, -1, nullptr);

  GtkSnapshot *snapshot = gtk_snapshot_new();
  GTK_WIDGET_GET_CLASS(GTK_WIDGET(view))->snapshot(GTK_WIDGET(view), snapshot);
  GskRenderNode *node = gtk_snapshot_free_to_node(snapshot);
  if (node == nullptr) {
    // A view that paints nothing at all: a transparent image rather than a
    // failure, so a test can assert that nothing was drawn.
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
    if (window != nullptr) {
      gtk_window_set_child(GTK_WINDOW(window), nullptr);
      gtk_window_destroy(GTK_WINDOW(window));
    }
    return RnPixels(width, height, std::vector<guchar>(count, 0), "none");
  }

  GskRenderer *renderer = gsk_gl_renderer_new();
  std::string name = "GskGLRenderer";
  static bool announced = false;
  if (!gsk_renderer_realize(renderer, nullptr, nullptr)) {
    g_object_unref(renderer);
    renderer = gsk_cairo_renderer_new();
    name = "GskCairoRenderer";
    if (!gsk_renderer_realize(renderer, nullptr, nullptr)) {
      g_object_unref(renderer);
      gsk_render_node_unref(node);
      if (window != nullptr) {
        gtk_window_set_child(GTK_WINDOW(window), nullptr);
        gtk_window_destroy(GTK_WINDOW(window));
      }
      return RnPixels(width, height, {}, "");
    }
  }

  if (!announced) {
    // Once per run, into the log a failing CI job prints: which renderer
    // answered decides what a pixel assertion is allowed to ask about, the
    // cairo one drawing a transformed subtree unrotated.
    //
    // Printed rather than logged, and that is the point. This was a `g_message`
    // until 2026-10-10, when `gtk_paint_a_blend_sees_the_siblings_beneath_it`
    // failed on one CI shard and passed on a re-run of the same commit -- and
    // the first thing to check, which renderer had answered, was not in the
    // log: GLib's message went somewhere that job's output did not carry, while
    // the suite's own stdout did. So this goes where the test names go.
    // backlog/testing.md records the flake.
    announced = true;
    std::printf("rendering assertions are using %s\n", name.c_str());
    std::fflush(stdout);
  }

  graphene_rect_t viewport;
  graphene_rect_init(
      &viewport, 0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height));
  GdkTexture *texture = gsk_renderer_render_texture(renderer, node, &viewport);

  const std::size_t stride = static_cast<std::size_t>(width) * 4;
  std::vector<guchar> bytes(stride * static_cast<std::size_t>(height));
  gdk_texture_download(texture, bytes.data(), stride);

  g_object_unref(texture);
  gsk_renderer_unrealize(renderer);
  g_object_unref(renderer);
  gsk_render_node_unref(node);
  if (window != nullptr) {
    // Taken out of the window first: destroying a window unparents its child,
    // and the caller still owns the view.
    gtk_window_set_child(GTK_WINDOW(window), nullptr);
    gtk_window_destroy(GTK_WINDOW(window));
  }
  return RnPixels(width, height, std::move(bytes), name);
}

} // namespace basalt::testing
