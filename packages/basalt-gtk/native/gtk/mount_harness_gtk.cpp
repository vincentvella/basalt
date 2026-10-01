// Drives real ShadowViewMutations through the real GtkMountingManager into
// real GTK widgets, with no JS runtime, no Hermes and no Metro.
//
// Note that executeMount only *queues*: in a real host it is called on the JS
// thread and marshals to the GTK main thread. Calling it from the main thread
// here takes the same path, one main-loop turn later.
//
// This is the checkpoint between "the mounting manager compiles" and "React
// Native runs": it exercises the actual mutation walk against the actual
// Fabric types, so a mistake in the Create/Insert/Update/Remove/Delete
// handling shows up on screen rather than in review.

#include "GtkMountingManager.h"

#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/LayoutMetrics.h>
#include <react/renderer/graphics/Color.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

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

basalt::GtkMountingManager *g_mountingManager = nullptr;

// Second transaction: recolour one view, and remove another entirely. Fabric
// always emits Remove before Delete, and this reproduces that ordering.
gboolean applySecondTransaction(gpointer /*user_data*/) {
  g_message("--- transaction 2: update tag 2, remove+delete tag 3 ---");

  ShadowViewMutationList mutations;

  // Update: same tag, new props. Recolour blue -> purple and shrink.
  const ShadowView oldBlue = makeShadowView(2, 32, 32, 240, 160, 0.30f, 0.55f, 0.95f);
  const ShadowView newBlue = makeShadowView(2, 32, 32, 240, 100, 0.60f, 0.35f, 0.90f);
  mutations.push_back(ShadowViewMutation::UpdateMutation(oldBlue, newBlue, kSurfaceId));

  // Remove then Delete, the order Fabric guarantees.
  const ShadowView orange = makeShadowView(3, 296, 32, 240, 160, 0.95f, 0.45f, 0.35f);
  mutations.push_back(ShadowViewMutation::RemoveMutation(kSurfaceId, orange, 1));
  mutations.push_back(ShadowViewMutation::DeleteMutation(orange));

  g_mountingManager->executeMount(kSurfaceId, makeTransaction(2, std::move(mutations)));

  g_message("--- transaction 2 queued ---");
  return G_SOURCE_REMOVE;
}

void onActivate(GtkApplication *app, gpointer /*user_data*/) {
  GtkWidget *window = gtk_application_window_new(app);
  gtk_window_set_title(GTK_WINDOW(window), "basalt-core — mount harness");
  gtk_window_set_default_size(GTK_WINDOW(window), 640, 420);

  // The host owns the root: Fabric never emits a Create for it.
  RnView *root = g_mountingManager->createSurfaceRoot(kSurfaceId);
  gtk_window_set_child(GTK_WINDOW(window), GTK_WIDGET(root));

  g_message("--- transaction 1: create + insert 4 views ---");

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

  g_mountingManager->executeMount(kSurfaceId, makeTransaction(1, std::move(mutations)));

  g_message("--- transaction 1 queued (applies on the next main-loop turn) ---");

  gtk_window_present(GTK_WINDOW(window));

  g_timeout_add(2000, applySecondTransaction, nullptr);
}

} // namespace

int main(int argc, char **argv) {
  // Constructed on the main thread: GtkMountingManager records this thread and
  // asserts every executeMount arrives on it.
  basalt::GtkMountingManager mountingManager;
  g_mountingManager = &mountingManager;

  GtkApplication *app =
      gtk_application_new("dev.basalt.mountharness", G_APPLICATION_DEFAULT_FLAGS);
  g_signal_connect(app, "activate", G_CALLBACK(onActivate), nullptr);
  const int status = g_application_run(G_APPLICATION(app), argc, argv);
  g_object_unref(app);
  return status;
}
