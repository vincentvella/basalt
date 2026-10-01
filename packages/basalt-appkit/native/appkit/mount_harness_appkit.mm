// Drives real ShadowViewMutations through the real AppKitMountingManager into real
// NSViews, with no JS runtime, no Hermes and no Metro.
//
// The mirror of gtk/mount_harness_gtk.cpp, with the same two transactions and the
// same boxes, which is the point: the two platforms now share their mutation
// walk, and the cheapest way to notice that sharing has broken is to put the
// two pictures side by side.
//
// Note that executeMount only *queues*: in a real host it is called on the JS
// thread and marshals to the main queue. Calling it from the main thread here
// takes the same path, one run-loop turn later -- which is why this drains the
// run loop rather than assuming anything has happened.
//
// With BASALT_SNAPSHOT_DIR set it renders both transactions to PNGs and exits.
// Without it, it opens a window and applies the second transaction after two
// seconds, the way the GTK harness does.

#import "AppKitMountingManager.h"
#import "AppKitSnapshot.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <utility>

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

ShadowView makeShadowView(Tag tag,
                          float x, float y, float width, float height,
                          float red, float green, float blue,
                          float opacity = 1.0f) {
  auto props = std::make_shared<ViewProps>();
  props->backgroundColor = facebook::react::colorFromComponents(
      ColorComponents{.red = red, .green = green, .blue = blue, .alpha = 1.0f});
  props->opacity = opacity;

  LayoutMetrics layoutMetrics;
  layoutMetrics.frame = {.origin = {.x = x, .y = y},
                         .size = {.width = width, .height = height}};

  ShadowView shadowView;
  shadowView.componentName = "View";
  shadowView.surfaceId = kSurfaceId;
  shadowView.tag = tag;
  shadowView.props = props;
  shadowView.layoutMetrics = layoutMetrics;
  return shadowView;
}

MountingTransaction makeTransaction(MountingTransaction::Number number,
                                    ShadowViewMutationList &&mutations) {
  return MountingTransaction(kSurfaceId, number, std::move(mutations), TransactionTelemetry{});
}

// First transaction: create four views and place them, one of them nested.
ShadowViewMutationList firstTransaction() {
  ShadowViewMutationList mutations;

  const ShadowView blue = makeShadowView(2, 32, 32, 240, 160, 0.30f, 0.55f, 0.95f);
  const ShadowView orange = makeShadowView(3, 296, 32, 240, 160, 0.95f, 0.45f, 0.35f);
  const ShadowView green = makeShadowView(4, 32, 224, 504, 140, 0.35f, 0.80f, 0.55f);
  const ShadowView nested = makeShadowView(5, 24, 24, 120, 90, 1.0f, 1.0f, 1.0f, 0.85f);

  // Fabric emits every Create before the Inserts that place them.
  mutations.push_back(ShadowViewMutation::CreateMutation(blue));
  mutations.push_back(ShadowViewMutation::CreateMutation(orange));
  mutations.push_back(ShadowViewMutation::CreateMutation(green));
  mutations.push_back(ShadowViewMutation::CreateMutation(nested));

  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, blue, 0));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, orange, 1));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, green, 2));
  mutations.push_back(ShadowViewMutation::InsertMutation(blue.tag, nested, 0));

  return mutations;
}

// Second transaction: recolour one view, and remove another entirely. Fabric
// always emits Remove before Delete, and this reproduces that ordering.
ShadowViewMutationList secondTransaction() {
  ShadowViewMutationList mutations;

  // Update: same tag, new props. Recolour blue -> purple and shrink.
  const ShadowView oldBlue = makeShadowView(2, 32, 32, 240, 160, 0.30f, 0.55f, 0.95f);
  const ShadowView newBlue = makeShadowView(2, 32, 32, 240, 100, 0.60f, 0.35f, 0.90f);
  mutations.push_back(ShadowViewMutation::UpdateMutation(oldBlue, newBlue, kSurfaceId));

  // Remove then Delete, the order Fabric guarantees.
  const ShadowView orange = makeShadowView(3, 296, 32, 240, 160, 0.95f, 0.45f, 0.35f);
  mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, orange, 1));
  mutations.push_back(ShadowViewMutation::DeleteMutation(orange));

  return mutations;
}

// executeMount hands the transaction to the main queue, so nothing has actually
// mounted when it returns. Spinning the run loop is what makes the queued block
// run -- and doing it explicitly rather than sleeping is what keeps this from
// being flaky.
void drainMainQueue() {
  [[NSRunLoop mainRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.05]];
}

int runSnapshots(basalt::AppKitMountingManager &manager, const char *directory) {
  RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
  [root setRnFrameX:0 y:0 width:640 height:420];
  [root setRnBackgroundColorRed:0.12 green:0.13 blue:0.16 alpha:1.0 hasColor:YES];

  manager.executeMount(kSurfaceId, makeTransaction(1, firstTransaction()));
  drainMainQueue();
  NSString *first = [NSString stringWithFormat:@"%s/mount-1.png", directory];
  if (!RnAppKitWriteSnapshot(root, first)) {
    return 1;
  }
  std::fputs([[root describeTree] UTF8String], stdout);

  manager.executeMount(kSurfaceId, makeTransaction(2, secondTransaction()));
  drainMainQueue();
  NSString *second = [NSString stringWithFormat:@"%s/mount-2.png", directory];
  if (!RnAppKitWriteSnapshot(root, second)) {
    return 1;
  }
  std::fputs("---\n", stdout);
  std::fputs([[root describeTree] UTF8String], stdout);

  manager.destroySurfaceRoot(kSurfaceId);
  return 0;
}

} // namespace

int main(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    // Constructed on the main thread: AppKitMountingManager records this thread and
    // asserts every transaction arrives on it.
    basalt::AppKitMountingManager manager;

    if (const char *directory = getenv("BASALT_SNAPSHOT_DIR")) {
      return runSnapshots(manager, directory);
    }

    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

    // The host owns the root: Fabric never emits a Create for it.
    RnAppKitView *root = manager.createSurfaceRoot(kSurfaceId);
    [root setRnFrameX:0 y:0 width:640 height:420];
    [root setRnBackgroundColorRed:0.12 green:0.13 blue:0.16 alpha:1.0 hasColor:YES];

    NSWindow *window =
        [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 640, 420)
                                    styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    window.title = @"basalt-core — macOS mount harness";
    window.contentView = root;
    [window center];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    manager.executeMount(kSurfaceId, makeTransaction(1, firstTransaction()));

    // Captured by pointer: a block copies what it captures, and a mounting
    // manager is neither copyable nor const-callable.
    basalt::AppKitMountingManager *pending = &manager;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(2.0 * NSEC_PER_SEC)),
                   dispatch_get_main_queue(),
                   ^{
                     pending->executeMount(kSurfaceId, makeTransaction(2, secondTransaction()));
                   });

    [NSApp run];
  }
  return 0;
}
