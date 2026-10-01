// Tests for GtkMountingManager: does a stream of ShadowViewMutations produce
// the widget tree it describes?
//
// This is the layer worth testing hardest. Everything above it is React
// Native's own code, everything below is GTK's, and the mutation walk is where
// this project can be wrong on its own.

#include "TestHarness.h"
#include "TreeDump.h"

#include "GtkMountingManager.h"

#include <string>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>

#include <cstdint>

using facebook::react::ColorComponents;
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

ShadowView makeView(Tag tag,
                    float x,
                    float y,
                    float width,
                    float height,
                    float red = 0.0F,
                    float green = 0.0F,
                    float blue = 0.0F) {
  auto props = std::make_shared<ViewProps>();
  props->backgroundColor = facebook::react::colorFromComponents(
      ColorComponents{.red = red, .green = green, .blue = blue, .alpha = 1.0F});

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = x, .y = y}, .size = {.width = width, .height = height}};

  ShadowView view;
  view.componentName = "View";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

// Applies mutations synchronously. executeMount would queue onto the main loop,
// which a test has no reason to spin.
void apply(basalt::GtkMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

int childCount(RnView *view) {
  int count = 0;
  for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(view)); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    ++count;
  }
  return count;
}

// The tag of the nth child, or -1.
int childTagAt(RnView *view, int index) {
  int i = 0;
  for (GtkWidget *child = gtk_widget_get_first_child(GTK_WIDGET(view)); child != nullptr;
       child = gtk_widget_get_next_sibling(child)) {
    if (i == index) {
      return rn_view_get_tag(RN_VIEW(child));
    }
    ++i;
  }
  return -1;
}

} // namespace

TEST(create_and_insert_builds_the_tree) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(10, 0, 0, 100, 50)));
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(11, 0, 60, 100, 50)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(10, 0, 0, 100, 50), 0));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(11, 0, 60, 100, 50), 1));
  apply(manager, std::move(mutations));

  EXPECT_EQ(childCount(root), 2);
  EXPECT_EQ(childTagAt(root, 0), 10);
  EXPECT_EQ(childTagAt(root, 1), 11);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(insert_honours_the_index) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList first;
  first.push_back(ShadowViewMutation::CreateMutation(makeView(20, 0, 0, 10, 10)));
  first.push_back(ShadowViewMutation::CreateMutation(makeView(21, 0, 0, 10, 10)));
  first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(20, 0, 0, 10, 10), 0));
  first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(21, 0, 0, 10, 10), 1));
  apply(manager, std::move(first));

  // Fabric numbers inserts by their position in the parent's final child list,
  // so a later mutation can land between two existing children.
  ShadowViewMutationList second;
  second.push_back(ShadowViewMutation::CreateMutation(makeView(22, 0, 0, 10, 10)));
  second.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(22, 0, 0, 10, 10), 1));
  apply(manager, std::move(second));

  EXPECT_EQ(childCount(root), 3);
  EXPECT_EQ(childTagAt(root, 0), 20);
  EXPECT_EQ(childTagAt(root, 1), 22);
  EXPECT_EQ(childTagAt(root, 2), 21);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(remove_detaches_but_does_not_destroy) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(30, 0, 0, 10, 10)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
  apply(manager, std::move(mutations));

  ShadowViewMutationList removal;
  removal.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
  apply(manager, std::move(removal));

  // Detached from the parent...
  EXPECT_EQ(childCount(root), 0);

  // ...but still alive, because a re-Insert may follow before the Delete. This
  // is the invariant the registry's strong reference exists for.
  ShadowViewMutationList reinsert;
  reinsert.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
  apply(manager, std::move(reinsert));

  EXPECT_EQ(childCount(root), 1);
  EXPECT_EQ(childTagAt(root, 0), 30);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(update_changes_frame_without_reparenting) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(40, 5, 6, 100, 50)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(40, 5, 6, 100, 50), 0));
  apply(manager, std::move(mutations));

  ShadowViewMutationList update;
  update.push_back(ShadowViewMutation::UpdateMutation(
      makeView(40, 5, 6, 100, 50), makeView(40, 7, 8, 200, 75), kSurfaceId));
  apply(manager, std::move(update));

  EXPECT_EQ(childCount(root), 1);

  graphene_rect_t frame;
  rn_view_get_frame(RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(root))), &frame);
  EXPECT_NEAR(frame.origin.x, 7.0, 0.001);
  EXPECT_NEAR(frame.origin.y, 8.0, 0.001);
  EXPECT_NEAR(frame.size.width, 200.0, 0.001);
  EXPECT_NEAR(frame.size.height, 75.0, 0.001);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(delete_removes_the_view_from_the_registry) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(50, 0, 0, 10, 10)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
  apply(manager, std::move(mutations));

  ShadowViewMutationList teardown;
  teardown.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
  teardown.push_back(ShadowViewMutation::DeleteMutation(makeView(50, 0, 0, 10, 10)));
  apply(manager, std::move(teardown));

  EXPECT_EQ(childCount(root), 0);

  // A stray Insert for a deleted tag must be ignored rather than crash, since
  // that is what a bug upstream would look like from here.
  ShadowViewMutationList stray;
  stray.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
  apply(manager, std::move(stray));
  EXPECT_EQ(childCount(root), 0);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(nested_children_are_parented_to_their_own_parent) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(60, 0, 0, 200, 200)));
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(61, 10, 10, 50, 50)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(60, 0, 0, 200, 200), 0));
  // parentTag 60, not the surface root.
  mutations.push_back(ShadowViewMutation::InsertMutation(60, makeView(61, 10, 10, 50, 50), 0));
  apply(manager, std::move(mutations));

  EXPECT_EQ(childCount(root), 1);
  RnView *outer = RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(root)));
  EXPECT_EQ(rn_view_get_tag(outer), 60);
  EXPECT_EQ(childCount(outer), 1);
  EXPECT_EQ(childTagAt(outer, 0), 61);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(surface_root_is_reused_not_recreated) {
  basalt::GtkMountingManager manager;
  RnView *first = manager.createSurfaceRoot(kSurfaceId);
  RnView *again = manager.createSurfaceRoot(kSurfaceId);

  // Fabric emits no Create for a root, so asking twice must not produce two.
  EXPECT(first == again);
  EXPECT(manager.getSurfaceRoot(kSurfaceId) == first);

  // In Fabric a SurfaceId *is* the root's tag.
  EXPECT_EQ(rn_view_get_tag(first), static_cast<int>(kSurfaceId));

  manager.destroySurfaceRoot(kSurfaceId);
  EXPECT(manager.getSurfaceRoot(kSurfaceId) == nullptr);
}

TEST(has_component_matches_the_registered_descriptors) {
  basalt::GtkMountingManager manager;

  EXPECT(manager.hasComponent("View"));
  EXPECT(manager.hasComponent("RootView"));
  EXPECT(manager.hasComponent("Paragraph"));
  EXPECT(manager.hasComponent("Image"));
  EXPECT(manager.hasComponent("ScrollView"));
  EXPECT(manager.hasComponent("TextInput"));
  EXPECT(manager.hasComponent("ActivityIndicatorView"));
  EXPECT(manager.hasComponent("Switch"));
  EXPECT(manager.hasComponent("ModalHostView"));
  EXPECT(manager.hasComponent("PullToRefreshView"));
  // What React Native substitutes for a component nobody registered. Mounting
  // it is what turns "nothing happens" into a view the tree dump can show.
  EXPECT(manager.hasComponent("UnimplementedNativeView"));

  // Claiming a component without a GTK peer is worse than admitting the gap:
  // the registry would build shadow nodes nothing can mount.
  //
  // `ActivityIndicator` is here rather than above on purpose: the component
  // React Native actually mounts is `ActivityIndicatorView`, and answering to
  // the name a reader expects would be answering to a name nothing sends.
  EXPECT(!manager.hasComponent("Slider"));
  EXPECT(!manager.hasComponent("ActivityIndicator"));
  EXPECT(!manager.hasComponent("SomethingNobodyHasHeardOf"));
}

// Hover runs only for views that asked for it, and the ask arrives as props on
// every Create and Update. Recorded by the mutation walk, so a cursor crossing
// an app that uses no hover costs a hit test and nothing else.
TEST(a_hover_listener_is_remembered_from_the_props) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  ShadowView listening = makeView(10, 0, 0, 100, 50);
  auto props = std::make_shared<ViewProps>();
  props->events[facebook::react::ViewEvents::Offset::PointerEnter] = true;
  listening.props = props;

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(listening));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, listening, 0));
  apply(manager, std::move(mutations));

  EXPECT_EQ(manager.hoverListenersForTag(10),
            static_cast<std::uint16_t>(basalt::HoverListenerEnter));
  // A view that never asked is not stored at all.
  EXPECT_EQ(manager.hoverListenersForTag(kSurfaceId),
            static_cast<std::uint16_t>(basalt::HoverListenerNone));

  // An update can take the listener away again, and leaving the old mask behind
  // would mean dispatching to a view that stopped listening.
  ShadowViewMutationList updates;
  updates.push_back(
      ShadowViewMutation::UpdateMutation(listening, makeView(10, 0, 0, 100, 50), kSurfaceId));
  apply(manager, std::move(updates));
  EXPECT_EQ(manager.hoverListenersForTag(10),
            static_cast<std::uint16_t>(basalt::HoverListenerNone));

  manager.destroySurfaceRoot(kSurfaceId);
}

// --- transformOrigin --------------------------------------------------------
//
// `resolveTransform` folds the origin into the matrix, and all three hosts
// call it -- so this has been "passed through but never exercised" since it
// was written. What makes it checkable without a display is that the anchor
// is a claim about a point: scaling about the top-left must leave the
// top-left corner exactly where it was.
//
// The hosts anchor a transform at the view's centre, which is what an
// untouched `transformOrigin` means, so the matrix is in centre-relative
// coordinates and the top-left corner is at (-w/2, -h/2).

namespace {

ShadowView makeTransformed(Tag tag,
                           float width,
                           float height,
                           facebook::react::Transform transform,
                           facebook::react::TransformOrigin origin = {}) {
  // Fresh props rather than a copy of makeView's: BaseViewProps has a deleted
  // copy constructor at React Native 0.86, which is what a current Expo app
  // installs. Copying compiled against main and broke the version an app
  // actually has -- found by building a real create-expo-app, not here.
  ShadowView view = makeView(tag, 0.0F, 0.0F, width, height);
  auto props = std::make_shared<ViewProps>();
  props->transform = transform;
  props->transformOrigin = origin;
  view.props = props;
  return view;
}

// The dump this host wrote, as a string.
std::string treeOf(RnView *root) {
  char *described = rn_view_describe_tree(root);
  std::string tree = described != nullptr ? described : "";
  g_free(described);
  return tree;
}

} // namespace

TEST(transform_origin_anchors_the_corner_it_names) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  using facebook::react::Transform;
  using facebook::react::TransformOrigin;
  using facebook::react::UnitType;
  using facebook::react::ValueUnit;

  // Doubled about the top-left of a 100x50 view.
  TransformOrigin topLeft;
  topLeft.xy = {ValueUnit(0.0F, UnitType::Point), ValueUnit(0.0F, UnitType::Point)};

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(
      makeTransformed(30, 100.0F, 50.0F, Transform::Scale(2.0F, 2.0F, 1.0F), topLeft)));
  mutations.push_back(ShadowViewMutation::InsertMutation(
      kSurfaceId,
      makeTransformed(30, 100.0F, 50.0F, Transform::Scale(2.0F, 2.0F, 1.0F), topLeft),
      0));
  apply(manager, std::move(mutations));

  const auto matrix = basalt::testing::transformIn(treeOf(root), 30);
  EXPECT_EQ(matrix[0], 2.0);
  EXPECT_EQ(matrix[3], 2.0);

  // The corner the origin names, in the centre-relative coordinates the
  // matrix is written in, has to come back to itself: (-50,-25) scaled by two
  // is (-100,-50), so the translation has to put 50 and 25 back.
  const double cornerX = matrix[0] * -50.0 + matrix[2] * -25.0 + matrix[4];
  const double cornerY = matrix[1] * -50.0 + matrix[3] * -25.0 + matrix[5];
  EXPECT_EQ(cornerX, -50.0);
  EXPECT_EQ(cornerY, -25.0);

  manager.destroySurfaceRoot(kSurfaceId);
}

// And with no origin set, the centre is the anchor -- which is the default
// every host relies on, and the reason an unset origin must not be folded in
// as (0,0).
TEST(no_transform_origin_anchors_the_centre) {
  basalt::GtkMountingManager manager;
  RnView *root = manager.createSurfaceRoot(kSurfaceId);

  using facebook::react::Transform;

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(
      makeTransformed(31, 100.0F, 50.0F, Transform::Scale(2.0F, 2.0F, 1.0F))));
  mutations.push_back(ShadowViewMutation::InsertMutation(
      kSurfaceId, makeTransformed(31, 100.0F, 50.0F, Transform::Scale(2.0F, 2.0F, 1.0F)), 0));
  apply(manager, std::move(mutations));

  const auto matrix = basalt::testing::transformIn(treeOf(root), 31);
  EXPECT_EQ(matrix[0], 2.0);
  EXPECT_EQ(matrix[3], 2.0);
  // No translation at all: the centre is already the anchor.
  EXPECT_EQ(matrix[4], 0.0);
  EXPECT_EQ(matrix[5], 0.0);

  manager.destroySurfaceRoot(kSurfaceId);
}
