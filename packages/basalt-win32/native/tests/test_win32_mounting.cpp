// Tests for Win32MountingManager: does a stream of ShadowViewMutations produce
// the view tree it describes?
//
// Deliberately the same nine questions `tests/test_mounting.cpp` asks of GTK
// and `tests/test_appkit_mounting.mm` asks of AppKit, in the same order, with
// the same tags and frames. All three managers share their walk, so most of
// this is asking whether the sharing actually holds -- and the day it stops,
// these fail here rather than in an app.
//
// One is new and is about this platform specifically:
// `win32_delete_frees_the_view` exists because Windows is the first of the
// three where the registry's ownership is written out rather than delegated to
// a refcount or to ARC.

#include "TestHarness.h"
#include "TreeDump.h"

#include "Win32MountingManager.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>

#include <cstdint>
#include <sstream>
#include <vector>

using basalt::Win32MountingManager;
using basalt::win32::RnWin32View;
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

// Applies mutations synchronously. executeMount would post to the UI thread,
// which a test has no message loop to pump -- though with no host installed
// postToUiThread runs inline anyway, so this is about being explicit rather
// than about avoiding a hang.
void apply(Win32MountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

size_t childCount(const RnWin32View *view) {
  return view->children().size();
}

// The tag of the nth child, or -1.
int32_t childTagAt(const RnWin32View *view, size_t index) {
  if (index >= view->children().size()) {
    return -1;
  }
  return view->children()[index]->tag();
}

} // namespace

TEST(win32_create_and_insert_builds_the_tree) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(10, 0, 0, 100, 50)));
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(11, 0, 60, 100, 50)));
  mutations.push_back(
      ShadowViewMutation::InsertMutation(kSurfaceId, makeView(10, 0, 0, 100, 50), 0));
  mutations.push_back(
      ShadowViewMutation::InsertMutation(kSurfaceId, makeView(11, 0, 60, 100, 50), 1));
  apply(manager, std::move(mutations));

  EXPECT_EQ(childCount(root), size_t{2});
  EXPECT_EQ(childTagAt(root, 0), 10);
  EXPECT_EQ(childTagAt(root, 1), 11);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_insert_honours_the_index) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList first;
  first.push_back(ShadowViewMutation::CreateMutation(makeView(20, 0, 0, 10, 10)));
  first.push_back(ShadowViewMutation::CreateMutation(makeView(21, 0, 0, 10, 10)));
  first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(20, 0, 0, 10, 10), 0));
  first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(21, 0, 0, 10, 10), 1));
  apply(manager, std::move(first));

  // An Insert's index counts positions in the parent's *final* child list, so
  // this lands between the two. An append-only implementation passes every
  // other test in this file.
  ShadowViewMutationList second;
  second.push_back(ShadowViewMutation::CreateMutation(makeView(22, 0, 0, 10, 10)));
  second.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(22, 0, 0, 10, 10), 1));
  apply(manager, std::move(second));

  EXPECT_EQ(childCount(root), size_t{3});
  EXPECT_EQ(childTagAt(root, 0), 20);
  EXPECT_EQ(childTagAt(root, 1), 22);
  EXPECT_EQ(childTagAt(root, 2), 21);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_remove_detaches_but_does_not_destroy) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(30, 0, 0, 10, 10)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
  apply(manager, std::move(mutations));

  ShadowViewMutationList removal;
  removal.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
  apply(manager, std::move(removal));

  // A reparent is a Remove and an Insert with no Delete between them, so the
  // view has to survive the gap -- and the registry is the only thing holding
  // it. Asking the registry is the assertion: if Remove had destroyed it, this
  // would be null or a dangling pointer.
  EXPECT_EQ(childCount(root), size_t{0});
  EXPECT(manager.viewForTag(30) != nullptr);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_update_changes_frame_without_reparenting) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(40, 0, 0, 100, 50)));
  mutations.push_back(
      ShadowViewMutation::InsertMutation(kSurfaceId, makeView(40, 0, 0, 100, 50), 0));
  apply(manager, std::move(mutations));

  ShadowViewMutationList update;
  update.push_back(ShadowViewMutation::UpdateMutation(
      makeView(40, 0, 0, 100, 50), makeView(40, 5, 6, 70, 30), kSurfaceId));
  apply(manager, std::move(update));

  RnWin32View *view = manager.viewForTag(40);
  EXPECT(view != nullptr);
  EXPECT_EQ(childCount(root), size_t{1});
  EXPECT_NEAR(view->frame().x, 5.0, 0.001);
  EXPECT_NEAR(view->frame().y, 6.0, 0.001);
  EXPECT_NEAR(view->frame().width, 70.0, 0.001);
  EXPECT_NEAR(view->frame().height, 30.0, 0.001);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_delete_removes_the_view_from_the_registry) {
  Win32MountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(50, 0, 0, 10, 10)));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
  apply(manager, std::move(mutations));
  EXPECT(manager.viewForTag(50) != nullptr);

  // Fabric guarantees the Remove before the Delete, and this is that order.
  ShadowViewMutationList teardown;
  teardown.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
  teardown.push_back(ShadowViewMutation::DeleteMutation(makeView(50, 0, 0, 10, 10)));
  apply(manager, std::move(teardown));

  EXPECT(manager.viewForTag(50) == nullptr);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_nested_children_are_parented_to_their_own_parent) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(60, 0, 0, 100, 100)));
  mutations.push_back(ShadowViewMutation::CreateMutation(makeView(61, 10, 10, 20, 20)));
  mutations.push_back(
      ShadowViewMutation::InsertMutation(kSurfaceId, makeView(60, 0, 0, 100, 100), 0));
  // Parented to 60, not to the surface root.
  mutations.push_back(ShadowViewMutation::InsertMutation(60, makeView(61, 10, 10, 20, 20), 0));
  apply(manager, std::move(mutations));

  EXPECT_EQ(childCount(root), size_t{1});
  RnWin32View *middle = manager.viewForTag(60);
  EXPECT(middle != nullptr);
  EXPECT_EQ(childCount(middle), size_t{1});
  EXPECT_EQ(childTagAt(middle, 0), 61);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_surface_root_is_reused_not_recreated) {
  Win32MountingManager manager;
  RnWin32View *first = manager.createSurfaceRoot(kSurfaceId);
  RnWin32View *second = manager.createSurfaceRoot(kSurfaceId);

  // In Fabric a SurfaceId *is* the root node's tag, and Fabric emits no Create
  // for it -- the root shadow node is the base of every diff, so it must
  // already exist when the first transaction arrives. Asking twice must not
  // make two.
  EXPECT(first == second);

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_a_mutation_for_an_unknown_tag_is_survivable) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);

  // What a bug upstream, or a transaction racing a surface teardown, looks like
  // from down here. It must not be fatal.
  ShadowViewMutationList mutations;
  mutations.push_back(
      ShadowViewMutation::InsertMutation(kSurfaceId, makeView(999, 0, 0, 10, 10), 0));
  mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(998, 0, 0, 10, 10), 0));
  mutations.push_back(ShadowViewMutation::DeleteMutation(makeView(997, 0, 0, 10, 10)));
  apply(manager, std::move(mutations));

  EXPECT_EQ(childCount(root), size_t{0});

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_props_reach_the_view_through_a_mutation) {
  Win32MountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  ShadowViewMutationList mutations;
  mutations.push_back(
      ShadowViewMutation::CreateMutation(makeView(70, 0, 0, 64, 32, 1.0F, 0.0F, 0.0F)));
  mutations.push_back(ShadowViewMutation::InsertMutation(
      kSurfaceId, makeView(70, 0, 0, 64, 32, 1.0F, 0.0F, 0.0F), 0));
  apply(manager, std::move(mutations));

  // The whole trip: a ViewProps built here, through the walk, into the view's
  // own state and out again as the dump the three hosts compare.
  RnWin32View *view = manager.viewForTag(70);
  EXPECT(view != nullptr);
  EXPECT(view->hasBackgroundColor());
  EXPECT_EQ(view->describeTree(), std::string("view tag=70 frame=(0,0 64x32) bg=#ff0000ff\n"));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(win32_has_component_matches_the_registered_descriptors) {
  Win32MountingManager manager;

  // Two statements of one fact, and when they disagree the registry wins:
  // Fabric builds shadow nodes nothing can mount and the app renders blank
  // rectangles rather than reporting anything. See ComponentRegistryWin32.cpp.
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

  // Claiming a component this host cannot mount is worse than admitting the
  // gap. `ActivityIndicator` is here rather than above on purpose: the
  // component React Native actually mounts is `ActivityIndicatorView`, and
  // answering to the name a reader expects would be answering to a name
  // nothing sends.
  EXPECT(!manager.hasComponent("Slider"));
  EXPECT(!manager.hasComponent("ActivityIndicator"));
  EXPECT(!manager.hasComponent("SomethingNobodyHasHeardOf"));
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
//
// The same two tests are in the other two suites, with the same tags, for the
// same reason the ones above are.

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

} // namespace

TEST(transform_origin_anchors_the_corner_it_names) {
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);

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

  const auto matrix = basalt::testing::transformIn(root->describeTree(), 30);
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
  Win32MountingManager manager;
  RnWin32View *root = manager.createSurfaceRoot(kSurfaceId);

  using facebook::react::Transform;

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(
      makeTransformed(31, 100.0F, 50.0F, Transform::Scale(2.0F, 2.0F, 1.0F))));
  mutations.push_back(ShadowViewMutation::InsertMutation(
      kSurfaceId, makeTransformed(31, 100.0F, 50.0F, Transform::Scale(2.0F, 2.0F, 1.0F)), 0));
  apply(manager, std::move(mutations));

  const auto matrix = basalt::testing::transformIn(root->describeTree(), 31);
  EXPECT_EQ(matrix[0], 2.0);
  EXPECT_EQ(matrix[3], 2.0);
  // No translation at all: the centre is already the anchor.
  EXPECT_EQ(matrix[4], 0.0);
  EXPECT_EQ(matrix[5], 0.0);

  manager.destroySurfaceRoot(kSurfaceId);
}
