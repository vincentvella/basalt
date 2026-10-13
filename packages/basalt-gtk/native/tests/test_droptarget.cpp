// Which view a drag is offered to.
//
// `GtkDropTarget.cpp` walks up from the widget under the pointer looking for
// one that accepts what is being dragged, and until these tests nothing
// checked that walk: the AppKit peer of this file was added first, and
// sabotaging it there showed a host that stopped reading the `nativeID` marker
// altogether left the whole suite green.
//
// Two sources, and both are here because the host reads them together:
//
//  - the `nativeID` marker, `basalt-drop:files,text`, which is how a plain
//    `<View>` says so on a platform where React Native has no prop for it;
//  - `acceptDragAndDropTypes` on a `<TextInput>`, a real React Native prop
//    that lands on the view as a mask.
//
// A real window, because `gtk_widget_pick` answers from the widget tree and
// wants the widgets mapped -- the same reason tests/test_focus.cpp builds one.

#include "TestHarness.h"

#include "DragAndDrop.h"
#include "GtkDropTarget.h"
#include "GtkMountingManager.h"

#include <react/renderer/components/iostextinput/TextInputProps.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/PropsParserContext.h>
#include <react/renderer/core/RawProps.h>
#include <react/renderer/core/RawPropsParser.h>

#include <folly/dynamic.h>

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>

using facebook::react::ContextContainer;
using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::PropsParserContext;
using facebook::react::RawProps;
using facebook::react::RawPropsParser;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::SurfaceId;
using facebook::react::Tag;
using facebook::react::TextInputProps;
using facebook::react::TransactionTelemetry;
using facebook::react::ViewProps;

namespace {

constexpr SurfaceId kSurfaceId = 1;

ShadowView makeMarkedView(Tag tag, const std::string &nativeId, float x, float y, float size) {
  auto props = std::make_shared<ViewProps>();
  props->nativeId = nativeId;

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = x, .y = y}, .size = {.width = size, .height = size}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

const RawPropsParser &textInputParser() {
  static const RawPropsParser parser = []() {
    RawPropsParser prepared;
    prepared.prepare<TextInputProps>();
    return prepared;
  }();
  return parser;
}

ShadowView makeField(Tag tag, folly::dynamic raw) {
  static const auto contextContainer = std::make_shared<const ContextContainer>();
  PropsParserContext context{kSurfaceId, *contextContainer};

  RawProps rawProps{std::move(raw)};
  rawProps.parse(textInputParser());

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 200, .height = 40}};

  ShadowView view;
  view.componentName = "TextInput";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = std::make_shared<const TextInputProps>(context, TextInputProps{}, rawProps);
  view.layoutMetrics = metrics;
  return view;
}

struct Scene {
  basalt::GtkMountingManager manager;
  RnView *root;
  GtkWidget *window;

  Scene() : root(manager.createSurfaceRoot(kSurfaceId)), window(gtk_window_new()) {
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
    pump();
  }

  static void pump() {
    const gint64 deadline = g_get_monotonic_time() + 300000; // 300ms
    while (g_get_monotonic_time() < deadline) {
      while (g_main_context_iteration(nullptr, FALSE)) {
      }
      g_usleep(1000);
    }
  }
};

} // namespace

TEST(droptarget_a_marked_view_is_found_under_the_pointer) {
  Scene scene;
  scene.mount({makeMarkedView(10, "basalt-drop:files,text", 0, 0, 200)});

  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsFiles), 10);
  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsText), 10);
  // Outside it, where nothing accepts anything.
  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 300, 250, basalt::DropAcceptsFiles), 0);
}

// A marker naming one kind refuses the other, which is what makes the cursor
// say no before the drop rather than the app rejecting it after.
TEST(droptarget_a_marker_refuses_a_kind_it_did_not_name) {
  Scene scene;
  scene.mount({makeMarkedView(10, "basalt-drop:text", 0, 0, 200)});

  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsText), 10);
  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsFiles), 0);
}

// An unmarked view is not a target, and nor is one whose nativeID means
// something else -- which is most of them, a nativeID being how an app names a
// view to itself.
TEST(droptarget_an_unmarked_view_accepts_nothing) {
  Scene scene;
  scene.mount({makeMarkedView(10, "my-view", 0, 0, 200)});

  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsFiles), 0);
  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsText), 0);
}

// `acceptDragAndDropTypes`, which is the same walk reached through a prop
// instead of a marker. A field naming only text refuses a dragged file.
TEST(droptarget_a_field_accepts_the_types_it_named) {
  Scene scene;
  scene.mount({makeField(
      10, folly::dynamic::object("acceptDragAndDropTypes", folly::dynamic::array("text/plain")))});

  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsText), 10);
  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsFiles), 0);
}

TEST(droptarget_a_field_without_the_prop_accepts_nothing) {
  Scene scene;
  scene.mount({makeField(10, folly::dynamic::object("text", "hello"))});

  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsText), 0);
  EXPECT_EQ((int)basalt::dropTargetAt(scene.root, 20, 20, basalt::DropAcceptsFiles), 0);
}
