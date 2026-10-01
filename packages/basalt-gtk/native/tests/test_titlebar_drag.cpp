// Which parts of an app-drawn header drag the window.
//
// The same questions tests/test_win32_titlebar.cpp asks of Windows, in the
// same order: a label inside a drag region drags, a no-drag region inside one
// stays pressable, a drag region nested inside *that* drags again, and a point
// under nothing marked is ordinary client area. Most of what this tests is
// whether the two desktops still agree, which is why the shape is copied
// rather than reinvented.
//
// What it cannot test is the move itself. gdk_toplevel_begin_move hands the
// window to the compositor, which needs a real pointer grab from a real press;
// synthesising one needs a session this suite does not have. So the seam is
// the hit test -- the part that decides *whether* to begin a move -- and the
// begin itself is left to the end-to-end suite and to a person with a mouse.

#include "TestHarness.h"

#include "GtkTitleBarLayout.h"
#include "RnView.h"
#include "TitleBarRegions.h"

#include <sstream>

namespace {

RnView *addChild(RnView *parent,
                 int tag,
                 float x,
                 float y,
                 float width,
                 float height,
                 const char *nativeId = nullptr) {
  RnView *child = rn_view_new(tag);
  g_object_ref_sink(child);
  rn_view_set_frame(child, x, y, width, height);
  rn_view_set_native_id(child, nativeId);
  rn_view_insert_child(parent, child, 0);
  return child;
}

// A real, shown window, for the reason tests/test_hittest.cpp gives: this
// walks up from gtk_widget_pick, and pick skips widgets that are not mapped.
// A tree allocated in isolation would be picked as nothing at all -- and
// "nothing" answers false, which is also the honest answer for a point that
// does not drag. Every test below would pass without testing anything.
//
// That is what the first test guards against: it expects *true*, so if the
// tree ever stops being allocated the suite fails rather than going quietly
// green.
struct Scene {
  GtkWidget *window;
  RnView *root;

  explicit Scene(int width, int height) {
    root = rn_view_new(1);
    window = gtk_window_new();
    gtk_window_set_default_size(GTK_WINDOW(window), width, height);
    gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(root));
  }

  ~Scene() { gtk_window_destroy(GTK_WINDOW(window)); }

  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  void show() {
    gtk_widget_set_visible(window, TRUE);
    pump();
  }

  // Bounded, so a machine that never produces a frame fails an assertion
  // rather than hanging the suite.
  static void pump() {
    const gint64 deadline = g_get_monotonic_time() + 500000; // 500ms
    while (g_get_monotonic_time() < deadline) {
      while (g_main_context_iteration(nullptr, FALSE)) {
      }
      g_usleep(1000);
    }
  }
};

} // namespace

TEST(titlebar_a_label_inside_a_drag_region_drags_the_window) {
  Scene scene(800, 600);
  RnView *header = addChild(scene.root, 2, 0.0F, 0.0F, 800.0F, 32.0F, basalt::kTitleBarDragRegionId);
  // Unmarked, and inside the header: the marked ancestor decides.
  addChild(header, 3, 10.0F, 5.0F, 100.0F, 20.0F);
  scene.show();

  EXPECT(basalt::isTitleBarDragRegionAt(scene.root, 20.0, 10.0));
  EXPECT(basalt::isTitleBarDragRegionAt(scene.root, 400.0, 10.0));
}

TEST(titlebar_a_no_drag_region_inside_a_drag_region_stays_pressable) {
  Scene scene(800, 600);
  RnView *header = addChild(scene.root, 2, 0.0F, 0.0F, 800.0F, 32.0F, basalt::kTitleBarDragRegionId);
  RnView *button =
      addChild(header, 3, 600.0F, 0.0F, 50.0F, 32.0F, basalt::kTitleBarNoDragRegionId);
  scene.show();

  EXPECT(!basalt::isTitleBarDragRegionAt(scene.root, 620.0, 10.0));

  // And a drag region nested inside that button drags again: the nearest
  // marker decides, not the outermost.
  addChild(button, 4, 5.0F, 5.0F, 10.0F, 10.0F, basalt::kTitleBarDragRegionId);
  Scene::pump();
  EXPECT(basalt::isTitleBarDragRegionAt(scene.root, 607.0, 7.0));
}

TEST(titlebar_a_point_under_nothing_marked_is_ordinary_client_area) {
  Scene scene(800, 600);
  addChild(scene.root, 2, 0.0F, 0.0F, 800.0F, 32.0F, basalt::kTitleBarDragRegionId);
  addChild(scene.root, 3, 0.0F, 32.0F, 800.0F, 568.0F);
  scene.show();

  EXPECT(!basalt::isTitleBarDragRegionAt(scene.root, 400.0, 300.0));
  // A miss altogether -- outside the root -- is not a drag either.
  EXPECT(!basalt::isTitleBarDragRegionAt(scene.root, 900.0, 10.0));
}

TEST(titlebar_a_root_marked_whole_drags_everywhere_it_is_not_overridden) {
  // An app that wants its entire surface to drag, which is a real shape for a
  // small tool window, and the case that fails if the walk stops before the
  // root rather than after it.
  Scene scene(400, 300);
  rn_view_set_native_id(scene.root, basalt::kTitleBarDragRegionId);
  addChild(scene.root, 2, 100.0F, 100.0F, 80.0F, 40.0F, basalt::kTitleBarNoDragRegionId);
  scene.show();

  EXPECT(basalt::isTitleBarDragRegionAt(scene.root, 10.0, 10.0));
  EXPECT(!basalt::isTitleBarDragRegionAt(scene.root, 140.0, 120.0));
}

TEST(titlebar_drag_regions_are_safe_without_a_tree) {
  EXPECT(!basalt::isTitleBarDragRegionAt(nullptr, 0.0, 0.0));
}
