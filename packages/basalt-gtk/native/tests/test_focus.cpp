// Keyboard focus: what Tab stops on, in what order, and what Enter does.
//
// The manager is built standalone against a real GtkMountingManager and a real
// window, because focus is the one part of input that needs both: GTK will not
// give focus to a widget that is not mapped, and the chain this exercises is
// GTK's own rather than this project's.
//
// No emitter is attached to these hand-built shadow views, so the focus and
// blur events have nowhere to go and the click dispatch does nothing. What is
// observable is the part that decides where focus actually is, which is the
// part this project can get wrong on its own.

#include "TestHarness.h"

#include "GtkFocus.h"
#include "GtkMountingManager.h"

#include <react/renderer/components/view/ViewProps.h>

#include <cstdint>
#include <sstream>

using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

ShadowView makeView(Tag tag, float y, bool accessible) {
  auto props = std::make_shared<ViewProps>();
  props->accessible = accessible;

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = y}, .size = {.width = 200, .height = 40}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

// A window with a focus manager, in the order the host builds them: the manager
// exists before the window is shown, and the first transaction arrives after.
//
// The order matters and is not incidental. GTK gives focus to the first
// focusable widget when a window is shown, so a tree mounted *before* that
// would open with something already focused -- which the AppKit host does not
// do, and which would make an app see onFocus at startup on one desktop only.
// The real host shows an empty window and mounts into it, and so does this.
struct Scene {
  basalt::GtkMountingManager manager;
  RnView *root;
  GtkWidget *window;
  basalt::GtkFocusManager focus;

  Scene()
      : root(manager.createSurfaceRoot(kSurfaceId)),
        window(gtk_window_new()),
        focus(&manager, root) {

    gtk_window_set_default_size(GTK_WINDOW(window), 400, 400);
    gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(root));
    gtk_widget_set_visible(window, TRUE);
    pump();
  }

  ~Scene() {
    gtk_window_destroy(GTK_WINDOW(window));
    manager.destroySurfaceRoot(kSurfaceId);
  }

  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  void mount(std::initializer_list<ShadowView> views) {
    ShadowViewMutationList mutations;
    int index = 0;
    for (const ShadowView &view : views) {
      mutations.push_back(ShadowViewMutation::CreateMutation(view));
      mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, view, index++));
    }
    manager.applyTransaction(
        kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
    // GTK will not give focus to a widget that is not mapped, and mapping
    // happens in the frame clock's layout phase rather than when a child is
    // added.
    pump();
  }

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

TEST(focus_accessible_views_are_the_ones_tab_stops_on) {
  Scene scene;
  // React Native's `focusable` prop never reaches this platform, so `accessible`
  // is the signal -- and it is what <Pressable> sets on everything it renders.
  scene.mount({makeView(10, 0, true), makeView(11, 50, false), makeView(12, 100, true)});

  EXPECT(rn_view_get_focusable(scene.manager.viewForTag(10)));
  EXPECT(!rn_view_get_focusable(scene.manager.viewForTag(11)));
  EXPECT(rn_view_get_focusable(scene.manager.viewForTag(12)));
  // And it shows up in the tree dump, so the cross-host diff can say the prop
  // arrived on all three.
  char *tree = rn_view_describe_tree(scene.root);
  const std::string dump(tree);
  g_free(tree);
  EXPECT(dump.find("focusable") != std::string::npos);
}

TEST(focus_tab_visits_focusable_views_in_tree_order) {
  Scene scene;
  scene.mount({makeView(10, 0, true), makeView(11, 50, false), makeView(12, 100, true)});
  // Nothing is focused when a window opens, on either desktop.
  EXPECT_EQ((long)scene.focus.focusedTag(), 0L);

  EXPECT(scene.focus.moveFocus(true));
  EXPECT_EQ((long)scene.focus.focusedTag(), 10L);
  // 11 is not accessible, so Tab goes past it.
  EXPECT(scene.focus.moveFocus(true));
  EXPECT_EQ((long)scene.focus.focusedTag(), 12L);
}

TEST(focus_tab_wraps_at_the_end) {
  Scene scene;
  scene.mount({makeView(10, 0, true), makeView(11, 50, true)});
  scene.focus.moveFocus(true);
  scene.focus.moveFocus(true);
  EXPECT_EQ((long)scene.focus.focusedTag(), 11L);
  // GTK's own `gtk_widget_child_focus` stops at the last child, because a real
  // window would carry on into whatever is next -- and here the surface root
  // *is* the content. The AppKit side wraps, so this one has to as well.
  EXPECT(scene.focus.moveFocus(true));
  EXPECT_EQ((long)scene.focus.focusedTag(), 10L);
}

TEST(focus_shift_tab_goes_back) {
  Scene scene;
  scene.mount({makeView(10, 0, true), makeView(11, 50, true)});
  scene.focus.moveFocus(true);
  scene.focus.moveFocus(true);
  EXPECT_EQ((long)scene.focus.focusedTag(), 11L);
  EXPECT(scene.focus.moveFocus(false));
  EXPECT_EQ((long)scene.focus.focusedTag(), 10L);
}

TEST(focus_activating_nothing_is_not_a_click) {
  Scene scene;
  scene.mount({makeView(10, 0, true)});
  // Nothing has focus yet, so there is nothing to activate -- and reporting a
  // press with no target is how a keystroke reaches the wrong view.
  EXPECT(!scene.focus.activateFocused());

  scene.focus.moveFocus(true);
  // No emitter is attached to these hand-built shadow views, so the click has
  // nowhere to deliver. It must not crash: that is exactly the shape of Enter
  // arriving between a Remove and its Delete.
  EXPECT(scene.focus.activateFocused());
}

TEST(focus_a_view_that_stops_being_accessible_leaves_the_tab_order) {
  Scene scene;
  scene.mount({makeView(10, 0, true), makeView(11, 50, true)});

  ShadowViewMutationList updates;
  updates.push_back(
      ShadowViewMutation::UpdateMutation(makeView(10, 0, true), makeView(10, 0, false), kSurfaceId));
  scene.manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 2, std::move(updates), TransactionTelemetry{}));

  EXPECT(!rn_view_get_focusable(scene.manager.viewForTag(10)));
  scene.focus.moveFocus(true);
  // Tab now goes straight to 11, because 10 has left the chain.
  EXPECT_EQ((long)scene.focus.focusedTag(), 11L);
}
