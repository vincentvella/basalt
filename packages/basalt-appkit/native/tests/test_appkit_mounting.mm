// Tests for AppKitMountingManager: does a stream of ShadowViewMutations produce
// the view tree it describes?
//
// Deliberately the same seven questions `tests/test_mounting.cpp` asks of the
// GTK one, in the same order, with the same tags and frames. The two managers
// share their walk now, so most of this is asking whether the sharing actually
// holds -- and the day it stops holding, these fail here rather than in an app.

#include "TestHarness.h"
#include "TreeDump.h"

#import "AppKitMountingManager.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/graphics/Color.h>

#include <sstream>
#include <vector>

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

// Applies mutations synchronously. executeMount would queue onto the main
// queue, which a test has no reason to spin.
void apply(basalt::AppKitMountingManager &manager, ShadowViewMutationList &&mutations) {
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
}

NSInteger childCount(RnAppKitView *view) {
  return (NSInteger)view.subviews.count;
}

// The tag of the nth child, or -1.
NSInteger childTagAt(RnAppKitView *view, NSInteger index) {
  if (index < 0 || index >= (NSInteger)view.subviews.count) {
    return -1;
  }
  return ((RnAppKitView *)view.subviews[(NSUInteger)index]).rnTag;
}

} // namespace

TEST(appkit_create_and_insert_builds_the_tree) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(10, 0, 0, 100, 50)));
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(11, 0, 60, 100, 50)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(10, 0, 0, 100, 50), 0));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(11, 0, 60, 100, 50), 1));
    apply(manager, std::move(mutations));

    EXPECT_EQ((long)childCount(root), 2L);
    EXPECT_EQ((long)childTagAt(root, 0), 10L);
    EXPECT_EQ((long)childTagAt(root, 1), 11L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(appkit_insert_honours_the_index) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList first;
    first.push_back(ShadowViewMutation::CreateMutation(makeView(20, 0, 0, 10, 10)));
    first.push_back(ShadowViewMutation::CreateMutation(makeView(21, 0, 0, 10, 10)));
    first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(20, 0, 0, 10, 10), 0));
    first.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(21, 0, 0, 10, 10), 1));
    apply(manager, std::move(first));

    // Fabric numbers inserts by their position in the parent's final child
    // list, so a later mutation can land between two existing children. On
    // AppKit that means addSubview:positioned:relativeTo: rather than a plain
    // append, and getting it wrong reorders the z-order too.
    ShadowViewMutationList second;
    second.push_back(ShadowViewMutation::CreateMutation(makeView(22, 0, 0, 10, 10)));
    second.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(22, 0, 0, 10, 10), 1));
    apply(manager, std::move(second));

    EXPECT_EQ((long)childCount(root), 3L);
    EXPECT_EQ((long)childTagAt(root, 0), 20L);
    EXPECT_EQ((long)childTagAt(root, 1), 22L);
    EXPECT_EQ((long)childTagAt(root, 2), 21L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(appkit_remove_detaches_but_does_not_destroy) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(30, 0, 0, 10, 10)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
    apply(manager, std::move(mutations));

    ShadowViewMutationList removal;
    removal.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
    apply(manager, std::move(removal));

    // Detached from the parent...
    EXPECT_EQ((long)childCount(root), 0L);

    // ...but still alive, because a re-Insert may follow before the Delete.
    // Under ARC that invariant rests on the registry's map entry rather than on
    // an explicit ref, which is exactly the sort of difference that would go
    // unnoticed until a view vanished mid-reparent.
    ShadowViewMutationList reinsert;
    reinsert.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(30, 0, 0, 10, 10), 0));
    apply(manager, std::move(reinsert));

    EXPECT_EQ((long)childCount(root), 1L);
    EXPECT_EQ((long)childTagAt(root, 0), 30L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(appkit_update_changes_frame_without_reparenting) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(40, 5, 6, 100, 50)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(40, 5, 6, 100, 50), 0));
    apply(manager, std::move(mutations));

    ShadowViewMutationList update;
    update.push_back(ShadowViewMutation::UpdateMutation(
        makeView(40, 5, 6, 100, 50), makeView(40, 7, 8, 200, 75), kSurfaceId));
    apply(manager, std::move(update));

    EXPECT_EQ((long)childCount(root), 1L);

    const NSRect frame = ((RnAppKitView *)root.subviews[0]).frame;
    EXPECT_NEAR(frame.origin.x, 7.0, 0.001);
    EXPECT_NEAR(frame.origin.y, 8.0, 0.001);
    EXPECT_NEAR(frame.size.width, 200.0, 0.001);
    EXPECT_NEAR(frame.size.height, 75.0, 0.001);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(appkit_delete_removes_the_view_from_the_registry) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(50, 0, 0, 10, 10)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
    apply(manager, std::move(mutations));

    ShadowViewMutationList teardown;
    teardown.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
    teardown.push_back(ShadowViewMutation::DeleteMutation(makeView(50, 0, 0, 10, 10)));
    apply(manager, std::move(teardown));

    EXPECT_EQ((long)childCount(root), 0L);
    EXPECT(manager.viewForTag(50) == nil);

    // A stray Insert for a deleted tag must be ignored rather than crash, since
    // that is what a bug upstream would look like from here.
    ShadowViewMutationList stray;
    stray.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(50, 0, 0, 10, 10), 0));
    apply(manager, std::move(stray));
    EXPECT_EQ((long)childCount(root), 0L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(appkit_nested_children_are_parented_to_their_own_parent) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(60, 0, 0, 200, 200)));
    mutations.push_back(ShadowViewMutation::CreateMutation(makeView(61, 10, 10, 50, 50)));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, makeView(60, 0, 0, 200, 200), 0));
    // parentTag 60, not the surface root.
    mutations.push_back(ShadowViewMutation::InsertMutation(60, makeView(61, 10, 10, 50, 50), 0));
    apply(manager, std::move(mutations));

    EXPECT_EQ((long)childCount(root), 1L);
    RnAppKitView *outer = (RnAppKitView *)root.subviews[0];
    EXPECT_EQ((long)outer.rnTag, 60L);
    EXPECT_EQ((long)childCount(outer), 1L);
    EXPECT_EQ((long)childTagAt(outer, 0), 61L);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

TEST(appkit_surface_root_is_reused_not_recreated) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *first = manager.createSurfaceRoot(kSurfaceId);
    RnAppKitView *again = manager.createSurfaceRoot(kSurfaceId);

    // Fabric emits no Create for a root, so asking twice must not produce two.
    EXPECT(first == again);
    EXPECT(manager.getSurfaceRoot(kSurfaceId) == first);

    // In Fabric a SurfaceId *is* the root's tag.
    EXPECT_EQ((long)first.rnTag, (long)kSurfaceId);

    manager.destroySurfaceRoot(kSurfaceId);
    EXPECT(manager.getSurfaceRoot(kSurfaceId) == nil);
  }
}

// Props travel the same path as the tree does, so a mutation that carries only
// new props has to land on the layer without anything else changing.
TEST(appkit_props_reach_the_layer_through_a_mutation) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(
        ShadowViewMutation::CreateMutation(makeView(70, 0, 0, 10, 10, 0.25F, 0.5F, 0.75F)));
    mutations.push_back(ShadowViewMutation::InsertMutation(
        kSurfaceId, makeView(70, 0, 0, 10, 10, 0.25F, 0.5F, 0.75F), 0));
    apply(manager, std::move(mutations));

    RnAppKitView *view = (RnAppKitView *)root.subviews[0];
    const CGFloat *components = CGColorGetComponents(view.layer.backgroundColor);
    EXPECT_NEAR(components[0], 0.25, 0.01);
    EXPECT_NEAR(components[1], 0.5, 0.01);
    EXPECT_NEAR(components[2], 0.75, 0.01);

    manager.destroySurfaceRoot(kSurfaceId);
  }
}

// What this platform claims, stated as one list rather than as assertions
// added a component at a time -- this test went stale three milestones running,
// because "everything not named here is missing" is the fact worth pinning and
// a pile of individual `!hasComponent` lines is not.
TEST(appkit_has_component_matches_the_registered_descriptors) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;

    // Paragraph is the mountable half of <Text>; Text and RawText exist only in
    // the shadow tree, folded into the Paragraph's AttributedString.
    // ScrollView's content child arrives as "ScrollContentView", which the
    // registry rewrites to "View" before it reaches here.
    const std::vector<std::string> supported = {
        "View", "RootView", "Paragraph", "ScrollView", "Image", "TextInput",
        // The controls, each of which is a real AppKit control rather than a
        // box this host draws -- plus UnimplementedNativeView, which is what
        // React Native substitutes for a component nobody registered.
        "ActivityIndicatorView", "Switch", "ModalHostView", "PullToRefreshView",
        "UnimplementedNativeView",
    };
    for (const auto &name : supported) {
      if (!manager.hasComponent(name)) {
        ::basalt::testing::recordFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__),
                                         "expected to support " + name);
      }
    }

    // Claiming a component without an AppKit peer is worse than admitting the
    // gap: the registry would build shadow nodes nothing can mount, and the app
    // would render blank rectangles rather than fail.
    // `ActivityIndicator` and `Modal` are here rather than above on purpose:
    // the components React Native actually mounts are `ActivityIndicatorView`
    // and `ModalHostView`, and answering to the names a reader expects would be
    // answering to names nothing sends.
    const std::vector<std::string> unsupported = {
        "Slider", "Modal", "ActivityIndicator", "SomethingNobodyHasHeardOf",
    };
    for (const auto &name : unsupported) {
      if (manager.hasComponent(name)) {
        ::basalt::testing::recordFailure(std::string(__FILE__) + ":" + std::to_string(__LINE__),
                                         "claims " + name + " without a peer");
      }
    }
  }
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
  @autoreleasepool {
  basalt::AppKitMountingManager manager;
  RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

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

  const auto matrix = basalt::testing::transformIn(std::string([root describeTree].UTF8String), 30);
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
}

// And with no origin set, the centre is the anchor -- which is the default
// every host relies on, and the reason an unset origin must not be folded in
// as (0,0).
TEST(no_transform_origin_anchors_the_centre) {
  @autoreleasepool {
  basalt::AppKitMountingManager manager;
  RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);

  using facebook::react::Transform;

  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(
      makeTransformed(31, 100.0F, 50.0F, Transform::Scale(2.0F, 2.0F, 1.0F))));
  mutations.push_back(ShadowViewMutation::InsertMutation(
      kSurfaceId, makeTransformed(31, 100.0F, 50.0F, Transform::Scale(2.0F, 2.0F, 1.0F)), 0));
  apply(manager, std::move(mutations));

  const auto matrix = basalt::testing::transformIn(std::string([root describeTree].UTF8String), 31);
  EXPECT_EQ(matrix[0], 2.0);
  EXPECT_EQ(matrix[3], 2.0);
  // No translation at all: the centre is already the anchor.
  EXPECT_EQ(matrix[4], 0.0);
  EXPECT_EQ(matrix[5], 0.0);

  manager.destroySurfaceRoot(kSurfaceId);
  }
}

// `accessibilityLabelledBy` through a real transaction, which is the half the
// view-level tests in test_appkit_accessibility.mm cannot reach: the prop names
// another view by its `nativeID`, and Fabric mounts a field before the label
// that follows it.
//
// Deliberately the same two cases the GTK suite asserts, in the same order and
// with the same tags, for the reason the rest of this file is: the resolution is
// shared in core/LabelRegistry.h, and the day the sharing stops holding these
// fail here rather than in an app.
namespace {

ShadowView makeNamedView(Tag tag, const std::string &nativeId) {
  ShadowView view = makeView(tag, 0, 0, 100, 50);
  auto props = std::make_shared<ViewProps>();
  props->nativeId = nativeId;
  view.props = props;
  return view;
}

ShadowView makeLabelledView(Tag tag, std::vector<std::string> ids) {
  ShadowView view = makeView(tag, 0, 0, 100, 50);
  auto props = std::make_shared<ViewProps>();
  props->accessibilityLabelledBy.value = std::move(ids);
  view.props = props;
  return view;
}

} // namespace

TEST(appkit_mounting_labelled_by_resolves_a_native_id) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList mutations;
    mutations.push_back(ShadowViewMutation::CreateMutation(makeLabelledView(10, {"name-label"})));
    mutations.push_back(
        ShadowViewMutation::InsertMutation(kSurfaceId, makeLabelledView(10, {"name-label"}), 0));
    mutations.push_back(ShadowViewMutation::CreateMutation(makeNamedView(20, "name-label")));
    mutations.push_back(
        ShadowViewMutation::InsertMutation(kSurfaceId, makeNamedView(20, "name-label"), 1));
    apply(manager, std::move(mutations));

    RnAppKitView *field = manager.viewForTag(10);
    EXPECT(field != nil);
    EXPECT(field.accessibilityTitleUIElement == manager.viewForTag(20));
  }
}

// The label in a later transaction, which is what a resolution done as the props
// arrived would miss.
TEST(appkit_mounting_labelled_by_waits_for_a_label_that_mounts_later) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    manager.createSurfaceRoot(kSurfaceId);

    ShadowViewMutationList first;
    first.push_back(ShadowViewMutation::CreateMutation(makeLabelledView(10, {"name-label"})));
    first.push_back(
        ShadowViewMutation::InsertMutation(kSurfaceId, makeLabelledView(10, {"name-label"}), 0));
    apply(manager, std::move(first));

    RnAppKitView *field = manager.viewForTag(10);
    EXPECT(field.accessibilityTitleUIElement == nil);

    ShadowViewMutationList second;
    second.push_back(ShadowViewMutation::CreateMutation(makeNamedView(20, "name-label")));
    second.push_back(
        ShadowViewMutation::InsertMutation(kSurfaceId, makeNamedView(20, "name-label"), 1));
    manager.applyTransaction(
        kSurfaceId, MountingTransaction(kSurfaceId, 2, std::move(second), TransactionTelemetry{}));

    EXPECT(field.accessibilityTitleUIElement == manager.viewForTag(20));
  }
}

// `accessibilityLiveRegion`: a status message read out when it changes.
//
// Nothing in an automated run is running VoiceOver, so what is asserted is the
// decision rather than the speech: `-rnAnnounce:assertive:` records the text it
// posted and these read it back. The rules -- what counts as a change -- are
// core/LiveRegions.h's and are tested there; this is the wiring, and it is the
// same four cases the GTK suite asserts.
namespace {

ShadowView makeStatusView(Tag tag,
                          facebook::react::AccessibilityLiveRegion politeness,
                          const std::string &label) {
  ShadowView view = makeView(tag, 0, 0, 200, 40);
  auto props = std::make_shared<ViewProps>();
  props->accessibilityLiveRegion = politeness;
  props->accessibilityLabel = label;
  view.props = props;
  return view;
}

void mountStatus(basalt::AppKitMountingManager &manager, const ShadowView &view, int transaction) {
  ShadowViewMutationList mutations;
  if (transaction == 1) {
    mutations.push_back(ShadowViewMutation::CreateMutation(view));
    mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, view, 0));
  } else {
    mutations.push_back(ShadowViewMutation::UpdateMutation(view, view, kSurfaceId));
  }
  manager.applyTransaction(
      kSurfaceId,
      MountingTransaction(kSurfaceId, transaction, std::move(mutations), TransactionTelemetry{}));
}

} // namespace

TEST(appkit_mounting_a_live_region_says_nothing_when_it_appears) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    manager.createSurfaceRoot(kSurfaceId);

    using facebook::react::AccessibilityLiveRegion;
    mountStatus(manager, makeStatusView(10, AccessibilityLiveRegion::Polite, "Saving"), 1);
    // Mounting a status line is not news: the screen appeared, nothing changed.
    EXPECT(manager.viewForTag(10).rnLastAnnouncement == nil);
  }
}

TEST(appkit_mounting_a_live_region_announces_a_change) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    manager.createSurfaceRoot(kSurfaceId);

    using facebook::react::AccessibilityLiveRegion;
    mountStatus(manager, makeStatusView(10, AccessibilityLiveRegion::Polite, "Saving"), 1);
    mountStatus(manager, makeStatusView(10, AccessibilityLiveRegion::Polite, "Saved"), 2);

    RnAppKitView *status = manager.viewForTag(10);
    EXPECT(status.rnLastAnnouncement != nil);
    EXPECT([status.rnLastAnnouncement isEqualToString:@"Saved"]);
  }
}

TEST(appkit_mounting_a_view_that_is_not_a_live_region_stays_quiet) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    manager.createSurfaceRoot(kSurfaceId);

    using facebook::react::AccessibilityLiveRegion;
    mountStatus(manager, makeStatusView(10, AccessibilityLiveRegion::None, "Saving"), 1);
    mountStatus(manager, makeStatusView(10, AccessibilityLiveRegion::None, "Saved"), 2);
    EXPECT(manager.viewForTag(10).rnLastAnnouncement == nil);
  }
}

TEST(appkit_mounting_a_live_region_that_stops_being_one_goes_quiet) {
  @autoreleasepool {
    basalt::AppKitMountingManager manager;
    manager.createSurfaceRoot(kSurfaceId);

    using facebook::react::AccessibilityLiveRegion;
    mountStatus(manager, makeStatusView(10, AccessibilityLiveRegion::Assertive, "Saving"), 1);
    mountStatus(manager, makeStatusView(10, AccessibilityLiveRegion::Assertive, "Saved"), 2);
    RnAppKitView *status = manager.viewForTag(10);
    EXPECT([status.rnLastAnnouncement isEqualToString:@"Saved"]);

    mountStatus(manager, makeStatusView(10, AccessibilityLiveRegion::None, "Saved again"), 3);
    // Still the old announcement: nothing new was said.
    EXPECT([status.rnLastAnnouncement isEqualToString:@"Saved"]);
  }
}

// The other source of a region's text: a <Text> inside it rather than a label on
// it. Asserted against the collector directly, a Paragraph with real state being
// more machinery than the question needs.
TEST(appkit_collected_text_is_every_paragraph_in_the_subtree) {
  @autoreleasepool {
    RnAppKitView *region = [RnAppKitView viewWithTag:1];
    [region setRnFrameX:0 y:0 width:200 height:100];
    EXPECT_EQ(region.rnCollectedText.length, 0UL);

    RnAppKitView *first = [RnAppKitView viewWithTag:2];
    [first setRnFrameX:0 y:0 width:200 height:20];
    [region insertRnChild:first atIndex:0];
    RnAppKitView *second = [RnAppKitView viewWithTag:3];
    [second setRnFrameX:0 y:20 width:200 height:20];
    [region insertRnChild:second atIndex:1];
    // No text layouts, so still nothing: a tree of plain views collects nothing
    // rather than crashing on the way down.
    EXPECT_EQ(region.rnCollectedText.length, 0UL);
  }
}
