// Which view a drag is offered to.
//
// `AppKitDropTarget.mm` walks up from the view under the pointer looking for
// one that accepts what is being dragged, and until these tests nothing
// checked that walk on this host: a change that stopped reading the `nativeID`
// marker altogether left the whole suite green. Found by sabotaging it while
// adding the second source of accepted kinds below.
//
// Two sources, and both are tested here because a host reads them together:
//
//  - the `nativeID` marker, `basalt-drop:files,text`, which is how a plain
//    `<View>` says so on a platform where React Native has no prop for it;
//  - `acceptDragAndDropTypes` on a `<TextInput>`, which is a real React Native
//    prop and lands on the view as a mask. Its own tests, including that the
//    field refuses a kind it did not name, are in test_appkit_textinput.mm
//    beside the rest of the field harness.

#include "TestHarness.h"

#import "AppKitDropTarget.h"
#import "AppKitMountingManager.h"
#import "RnAppKitView.h"

#include "DragAndDrop.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

#include <memory>
#include <sstream>
#include <string>

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

// Mounts views under the root in order, each as a child of the root.
void mountAll(basalt::AppKitMountingManager &manager, const std::vector<ShadowView> &views) {
  ShadowViewMutationList mutations;
  int index = 0;
  for (const ShadowView &view : views) {
    mutations.push_back(ShadowViewMutation::CreateMutation(view));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, view, index++));
  }
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

} // namespace

TEST(droptarget_a_marked_view_is_found_under_the_pointer) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];
    mountAll(manager, {makeMarkedView(10, "basalt-drop:files,text", 0, 0, 200)});

    EXPECT_EQ((int)basalt::dropTargetAt(root, 20, 20, basalt::DropAcceptsFiles), 10);
    EXPECT_EQ((int)basalt::dropTargetAt(root, 20, 20, basalt::DropAcceptsText), 10);
    // Outside it, where nothing accepts anything.
    EXPECT_EQ((int)basalt::dropTargetAt(root, 300, 250, basalt::DropAcceptsFiles), 0);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// A marker naming one kind refuses the other, which is what makes the cursor
// say no before the drop rather than the app rejecting it after.
TEST(droptarget_a_marker_refuses_a_kind_it_did_not_name) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];
    mountAll(manager, {makeMarkedView(10, "basalt-drop:text", 0, 0, 200)});

    EXPECT_EQ((int)basalt::dropTargetAt(root, 20, 20, basalt::DropAcceptsText), 10);
    EXPECT_EQ((int)basalt::dropTargetAt(root, 20, 20, basalt::DropAcceptsFiles), 0);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// An unmarked view is not a target, and nor is one whose nativeID means
// something else -- which is most of them, a nativeID being how an app names a
// view to itself.
TEST(droptarget_an_unmarked_view_accepts_nothing) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:400 height:300];
    mountAll(manager, {makeMarkedView(10, "my-view", 0, 0, 200)});

    EXPECT_EQ((int)basalt::dropTargetAt(root, 20, 20, basalt::DropAcceptsFiles), 0);
    EXPECT_EQ((int)basalt::dropTargetAt(root, 20, 20, basalt::DropAcceptsText), 0);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}
