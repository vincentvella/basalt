// `<Image defaultSource>` on GTK, against pixels.
//
// The rule is in core/ImagePlaceholder.h's comment and is React Native's:
// draw the placeholder until the source lands, and keep it if the source
// fails. Both halves are asserted here because both were missing -- a view
// whose remote image failed went empty on every host.
//
// Pixels rather than a texture pointer: `rn_view_set_texture` has no getter,
// and what an app sees is the drawing anyway. A one-pixel red PNG as the
// placeholder, so "the placeholder is on screen" is "this view is red".

#include "TestHarness.h"

#include "GtkMountingManager.h"
#include "GtkPixels.h"

#include <react/renderer/components/image/ImageProps.h>
#include <react/renderer/mounting/MountingTransaction.h>
#include <react/renderer/mounting/ShadowView.h>
#include <react/renderer/mounting/ShadowViewMutation.h>

#include <sstream>
#include <string>

using basalt::testing::renderView;
using facebook::react::ImageProps;
using facebook::react::LayoutMetrics;
using facebook::react::MountingTransaction;
using facebook::react::ShadowView;
using facebook::react::ShadowViewMutation;
using facebook::react::ShadowViewMutationList;
using facebook::react::Tag;
using facebook::react::TransactionTelemetry;

namespace {

constexpr auto kSurfaceId = 1;

// One opaque red pixel, so a placeholder that drew covers the view in red.
// Generated rather than copied: the first string tried here was RGBA with an
// alpha of 127, which drew correctly and failed an opacity assertion.
const char *kRedPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAIAAACQd1PeAAAADElEQVR4nGP4z8AAAAMBAQDJ/pLvAAAAAElFTkSuQmCC";

std::string redPngUri() {
  return std::string("data:image/png;base64,") + kRedPngBase64;
}

ShadowView makeImage(Tag tag, const std::function<void(ImageProps &)> &configure) {
  auto props = std::make_shared<ImageProps>();
  configure(*props);

  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0}, .size = {.width = 20, .height = 20}};

  ShadowView view;
  view.componentName = "Image";
  view.surfaceId = kSurfaceId;
  view.tag = tag;
  view.props = props;
  view.layoutMetrics = metrics;
  return view;
}

RnView *mountImage(basalt::GtkMountingManager &manager, const ShadowView &view) {
  ShadowViewMutationList mutations;
  mutations.push_back(ShadowViewMutation::CreateMutation(view));
  mutations.push_back(ShadowViewMutation::InsertMutation(kSurfaceId, view, 0));
  manager.applyTransaction(
      kSurfaceId, MountingTransaction(kSurfaceId, 1, std::move(mutations), TransactionTelemetry{}));
  RnView *root = manager.getSurfaceRoot(kSurfaceId);
  return RN_VIEW(gtk_widget_get_first_child(GTK_WIDGET(root)));
}

// The loader answers on the main loop, so both the placeholder and the failure
// need the loop pumped before the pixels mean anything.
void pump() {
  for (int i = 0; i < 20000; ++i) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(200);
  }
}

bool isRed(RnView *view) {
  const basalt::testing::RnPixels pixels = renderView(view, 20, 20);
  const basalt::testing::RnPixel pixel = pixels.at(10, 10);
  return pixel.red > 200 && pixel.green < 60 && pixel.blue < 60 && pixel.alpha > 200;
}

} // namespace

// The harness itself, which the other three depend on: a source that loads
// must draw, or "not red" means nothing anywhere below.
TEST(image_harness_draws_a_source_that_loads) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountImage(manager, makeImage(39, [](ImageProps &props) {
                              facebook::react::ImageSource source{};
                              source.uri = redPngUri();
                              props.sources = {source};
                            }));
  pump();
  EXPECT(isRed(view));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(image_draws_its_default_source_while_the_source_loads) {
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  // A source that cannot arrive, so what is on screen is the placeholder and
  // only the placeholder.
  RnView *view = mountImage(manager, makeImage(40, [](ImageProps &props) {
                              props.defaultSource.uri = redPngUri();
                              facebook::react::ImageSource source{};
                              source.uri = "file:///no/such/image/at/all.png";
                              props.sources = {source};
                            }));
  pump();
  EXPECT(isRed(view));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(image_keeps_its_default_source_when_the_source_fails) {
  // The same picture a moment later, which is the half that is a decision
  // rather than a race: `RCTImageComponentView` leaves the placeholder up when
  // a load fails, and every host here used to clear the view to nothing.
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountImage(manager, makeImage(41, [](ImageProps &props) {
                              props.defaultSource.uri = redPngUri();
                              facebook::react::ImageSource source{};
                              source.uri = "file:///still/not/here.png";
                              props.sources = {source};
                            }));
  pump();
  pump();
  EXPECT(isRed(view));

  manager.destroySurfaceRoot(kSurfaceId);
}

TEST(image_with_no_default_source_is_empty_when_its_source_fails) {
  // The control, and the reason the two above are not just measuring a view
  // that happens to be red: with no placeholder named, a failed load leaves
  // nothing drawn.
  basalt::GtkMountingManager manager;
  manager.createSurfaceRoot(kSurfaceId);

  RnView *view = mountImage(manager, makeImage(42, [](ImageProps &props) {
                              facebook::react::ImageSource source{};
                              source.uri = "file:///nothing/here/either.png";
                              props.sources = {source};
                            }));
  pump();
  EXPECT(!isRed(view));

  manager.destroySurfaceRoot(kSurfaceId);
}
