// The AppKit canvas: what a `<Canvas>` on Skia 3.x gets when it mounts.
//
// The 2.x canvas never had a test like this. It could not have one: its
// `RNSkMetalCanvasProvider` wanted a window, so the only proof was
// e2e/skia.tsx against an app nobody has in CI. 3.x's layer needs neither a
// window nor a screen -- test_skia_window_surface.mm measured that -- so the
// peer can be driven here with a real view, a real layer and a real Dawn
// surface.
//
// What is asserted is everything the peer decides: whether this component is
// its business, whether there is an id yet, the layer going into the tree at
// the right pixel size, a resize reconfiguring rather than reattaching, an id
// that moved, and the teardown. What is not asserted is a frame on the layer,
// for the reason test_skia_window_surface.mm records: a picture reaches a view
// only through a `jsi::Value`.

#include "TestHarness.h"

#include "AppKitSkiaPeer.h"
#include "BasaltSkiaCanvasAppKit.h"
#include "BasaltSkiaModule.h"
#include "JsRuntimeAccess.h"
#include "SkiaPictureViewComponent.h"
#include "SkiaViewComponent.h"

#import "RnAppKitView.h"

#import <AppKit/AppKit.h>

#include <hermes/hermes.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include "RNSkManager.h"
#pragma clang diagnostic pop

#include <ReactCommon/TurboModule.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/mounting/ShadowView.h>

#include <memory>
#include <optional>
#include <utility>
#include <sstream>
#include <string>

using facebook::react::LayoutMetrics;
using facebook::react::ShadowView;
using facebook::react::Tag;
using facebook::react::ViewProps;

namespace {

ShadowView makeShadowView(const char *componentName, Tag tag, float width, float height) {
  ShadowView view;
  view.componentName = componentName;
  view.surfaceId = 1;
  view.tag = tag;
  view.props = std::make_shared<ViewProps>();
  LayoutMetrics metrics;
  metrics.frame = {.origin = {.x = 0, .y = 0},
                   .size = {.width = width, .height = height}};
  view.layoutMetrics = metrics;
  return view;
}

// A view the way the mounting manager makes one, with a layer of its own: the
// canvas goes in as a sublayer of that.
RnAppKitView *makeView(NSString *nativeId) {
  RnAppKitView *view = [[RnAppKitView alloc] initWithFrame:NSMakeRect(0, 0, 100, 50)];
  view.wantsLayer = YES;
  view.rnNativeId = nativeId;
  return view;
}

// The module, installed once for the whole file. A canvas without a manager
// does nothing and says so, which is its own case below; everything after it
// needs Skia's bindings to exist.
struct Installed {
  std::unique_ptr<facebook::jsi::Runtime> runtime;
  std::shared_ptr<facebook::react::TurboModule> module;
};

Installed &installed() {
  static Installed state = [] {
    Installed state;
    state.runtime = facebook::hermes::makeHermesRuntime();
    state.module = basalt::makeSkiaTurboModule(basalt::BasaltSkiaModule::kModuleName,
                                               basalt::jsCallInvoker());
    if (state.runtime != nullptr && state.module != nullptr) {
      facebook::jsi::Value method = state.module->get(
          *state.runtime,
          facebook::jsi::PropNameID::forAscii(*state.runtime, "install"));
      if (method.isObject() && method.getObject(*state.runtime).isFunction(*state.runtime)) {
        method.getObject(*state.runtime).getFunction(*state.runtime).call(*state.runtime);
      }
    }
    return state;
  }();
  return state;
}

NSUInteger sublayerCount(RnAppKitView *view) {
  return view.layer.sublayers == nil ? 0 : view.layer.sublayers.count;
}

// Evaluates `source` in the runtime the module was installed into, and answers
// the result as a string. A JavaScript error is caught and returned with its
// message, so an assertion says what went wrong rather than taking the whole
// suite down through JSI.
std::string evaluate(const char *source) {
  Installed &state = installed();
  if (state.runtime == nullptr) {
    return "no runtime";
  }
  try {
    facebook::jsi::Value value = state.runtime->evaluateJavaScript(
        std::make_shared<facebook::jsi::StringBuffer>(source), "test_skia_canvas.js");
    return value.toString(*state.runtime).utf8(*state.runtime);
  } catch (const facebook::jsi::JSError &error) {
    return std::string("JSError: ") + error.getMessage();
  } catch (const std::exception &error) {
    return std::string("exception: ") + error.what();
  }
}

} // namespace


TEST(skia_canvas_is_not_interested_in_other_components) {
  // A filter rather than a question asked twice: the mounting manager calls
  // this for every view it touches, and false is how it learns to carry on.
  RnAppKitView *view = makeView(@"1");
  EXPECT(!basalt::applySkiaCanvas(view, makeShadowView("View", 10, 100, 50)));
  // Including the *other* Skia canvas. On a 3.x build a SkiaPictureView is a
  // 2.x component that nothing here draws, and claiming it would mean
  // reporting a canvas that never gets a picture.
  EXPECT(!basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaPictureViewComponentName, 11, 100, 50)));
  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 0);
}

TEST(skia_canvas_waits_for_the_native_id) {
  // A SkiaView is mounted before its nativeID has been applied in some orders,
  // and the next updateView carries it. Claimed -- it is this component -- and
  // nothing built yet.
  EXPECT(installed().runtime != nullptr);
  RnAppKitView *view = makeView(nil);
  EXPECT(basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaViewComponentName, 20, 100, 50)));
  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 0);

  // And a nativeID that is not a number is not an id. `<Canvas>` stringifies a
  // counter; anything else on a SkiaView is somebody else's prop.
  RnAppKitView *named = makeView(@"not-a-number");
  EXPECT(basalt::applySkiaCanvas(
      named, makeShadowView(facebook::react::SkiaViewComponentName, 21, 100, 50)));
  EXPECT_EQ(static_cast<int>(sublayerCount(named)), 0);
}

TEST(skia_canvas_sizes_its_surface_in_pixels) {
  // Separate from the peer because the peer cannot be asked this: the scale is
  // 1 on a machine with no host reporting one, and at scale 1 a peer that
  // multiplied and a peer that forgot produce the same number. This was found
  // by sabotage -- passing `frame.size` straight to `drawableSize` left every
  // assertion green.
  EXPECT(basalt::skiaCanvasPixelSize(100, 50, 2.0) == std::make_pair(200, 100));
  EXPECT(basalt::skiaCanvasPixelSize(100, 50, 1.0) == std::make_pair(100, 50));
  // Rounded, not truncated. A layout of 584.9999 points at 2x is 1170 pixels
  // and not 1169, which is the case upstream's own SkiaView.mm calls out.
  EXPECT(basalt::skiaCanvasPixelSize(584.9999, 584.9999, 2.0) == std::make_pair(1170, 1170));
  // A density nobody reported yet. Multiplying by zero would turn a laid-out
  // canvas into no canvas at all, which reads as a canvas that never mounted.
  EXPECT(basalt::skiaCanvasPixelSize(100, 50, 0.0) == std::make_pair(100, 50));
  // And a layout that has no size, which Yoga hands out before the first real
  // one. The peer attaches nothing for these.
  EXPECT(basalt::skiaCanvasPixelSize(0, 0, 2.0) == std::make_pair(0, 0));
}

TEST(skia_canvas_puts_a_metal_layer_in_the_view_at_the_pixel_size) {
  EXPECT(installed().runtime != nullptr);
  EXPECT(basalt::BasaltSkiaModule::manager() != nullptr);

  const Tag tag = 30;
  RnAppKitView *view = makeView(@"3");
  EXPECT(basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 100, 50)));

  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 1);
  CALayer *layer = sublayerCount(view) == 1 ? view.layer.sublayers.firstObject : nil;
  EXPECT(layer != nil);
  if (layer == nil) {
    return;
  }
  // A CAMetalLayer, which is what Dawn can make a surface over. Anything else
  // would attach and then fail to present.
  EXPECT([layer isKindOfClass:[CAMetalLayer class]]);

  // The layer's frame is in points, like every other layer's; its drawable is
  // in pixels. Getting that backwards draws at half or double size on a
  // Retina display, which is the bug the 2.x peer's comment warns about from
  // the other direction -- that one took points.
  EXPECT_EQ(static_cast<int>(layer.frame.size.width), 100);
  EXPECT_EQ(static_cast<int>(layer.frame.size.height), 50);
  const double density =
      basalt::BasaltSkiaModule::manager()->getPlatformContext()->getPixelDensity();
  const auto pixels = basalt::skiaCanvasPixelSize(100, 50, density);
  CAMetalLayer *metal = (CAMetalLayer *)layer;
  EXPECT_EQ(static_cast<int>(metal.drawableSize.width), pixels.first);
  EXPECT_EQ(static_cast<int>(metal.drawableSize.height), pixels.second);
  // And the surface, which is the thing that actually matters and is not
  // readable off the layer: CAMetalLayer takes a drawableSize and a swapchain
  // is a separate configuration over it.
  EXPECT(basalt::skiaCanvasAttachedSize(tag) == std::make_optional(pixels));

  // Mounted again at the same size -- which happens on every unrelated prop
  // change -- and it is the same layer rather than a second one.
  EXPECT(basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 100, 50)));
  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 1);
  EXPECT(view.layer.sublayers.firstObject == layer);

  // Resized the way a window resize resizes it: the swapchain follows, not
  // just the layer.
  EXPECT(basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 200, 120)));
  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 1);
  const auto resized = basalt::skiaCanvasPixelSize(200, 120, density);
  EXPECT_EQ(static_cast<int>(metal.drawableSize.width), resized.first);
  EXPECT_EQ(static_cast<int>(layer.frame.size.width), 200);
  EXPECT(basalt::skiaCanvasAttachedSize(tag) == std::make_optional(resized));

  // A zero-sized layout is left alone rather than attached: a surface of no
  // pixels is not a surface, and Yoga hands out zero sizes before the first
  // real layout. Asserted on the surface rather than the layer, because
  // CAMetalLayer ignores a drawableSize of zero and keeps the last one -- so
  // the layer cannot tell this case apart, which another sabotage proved.
  EXPECT(basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 0, 0)));
  EXPECT(basalt::skiaCanvasAttachedSize(tag) == std::make_optional(resized));

  basalt::forgetSkiaCanvas(tag);
  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 0);
  EXPECT(!basalt::skiaCanvasAttachedSize(tag).has_value());
}

TEST(skia_canvas_follows_a_native_id_that_moved) {
  // React can reuse a view and give it another canvas's id. Leaving the old
  // registration behind points the manager at a canvas nothing draws into, so
  // the peer re-registers -- and keeps the same layer, because the view is the
  // same view.
  EXPECT(installed().runtime != nullptr);
  const Tag tag = 40;
  RnAppKitView *view = makeView(@"4");
  EXPECT(basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 80, 40)));
  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 1);
  CALayer *layer = view.layer.sublayers.firstObject;

  view.rnNativeId = @"5";
  EXPECT(basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 80, 40)));
  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 1);
  EXPECT(view.layer.sublayers.firstObject == layer);

  basalt::forgetSkiaCanvas(tag);
  EXPECT_EQ(static_cast<int>(sublayerCount(view)), 0);
  // Twice is harmless: the mounting walk forgets a tag once, but a teardown
  // that threw on an unknown tag would be a crash in the one path that runs
  // while everything else is being destroyed.
  basalt::forgetSkiaCanvas(tag);
  basalt::forgetSkiaCanvas(9999);
}

TEST(skia_canvas_draws_a_picture_and_reads_it_back) {
  // The claim nothing else here makes: a `<Canvas>` on 3.x *draws*. Everything
  // above is about mounting -- a layer in the tree, a surface at the right
  // size -- and a canvas that mounts and shows nothing looks identical from
  // the outside, which is the trap docs/TESTING.md spends a section on.
  //
  // A picture reaches a view only through `RNSkView::setJsiProperties` with an
  // `RNJsi::ViewProperty`, which constructs from a `jsi::Value` and nothing
  // else -- so this goes the way an app goes: `SkiaApi` builds the picture in
  // JavaScript, `SkiaViewApi.setJsiProperty` pushes it to the view by id, and
  // `SkiaViewApi.makeImageSnapshot` reads back what the view rendered. The
  // same three calls e2e/skia.tsx makes, with no app and no window.
  EXPECT(installed().runtime != nullptr);
  EXPECT(basalt::BasaltSkiaModule::manager() != nullptr);

  // Both globals, which is the install this depends on.
  EXPECT_EQ(evaluate("typeof SkiaApi"), std::string("object"));
  EXPECT_EQ(evaluate("typeof SkiaViewApi"), std::string("object"));

  const Tag tag = 50;
  RnAppKitView *view = makeView(@"50");
  EXPECT(basalt::applySkiaCanvas(
      view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 32, 24)));
  EXPECT(basalt::skiaCanvasAttachedSize(tag).has_value());

  // Green over the left half of a 32x24 canvas, red over the right: two
  // rectangles rather than one fill, so that "drew something" and "drew it
  // where it was asked to" are separate claims. The pair e2e/skia.tsx uses,
  // and the reason test_appkit_image.mm gives for sampling away from edges:
  // antialiasing puts a boundary half a pixel either way.
  const std::string result = evaluate(R"JS(
    (function () {
      var green = SkiaApi.Paint();
      green.setColor(SkiaApi.Color('rgb(0, 255, 0)'));
      var red = SkiaApi.Paint();
      red.setColor(SkiaApi.Color('rgb(255, 0, 0)'));

      var recorder = SkiaApi.PictureRecorder();
      var canvas = recorder.beginRecording(SkiaApi.XYWHRect(0, 0, 32, 24));
      canvas.drawRect(SkiaApi.XYWHRect(0, 0, 16, 24), green);
      canvas.drawRect(SkiaApi.XYWHRect(16, 0, 16, 24), red);
      var picture = recorder.finishRecordingAsPicture();

      SkiaViewApi.setJsiProperty(50, 'picture', picture);

      var image = SkiaViewApi.makeImageSnapshot(50);
      var pixels = image.readPixels();
      function at(x, y) {
        var i = (y * image.width() + x) * 4;
        return pixels[i] + ',' + pixels[i + 1] + ',' + pixels[i + 2] + ',' + pixels[i + 3];
      }
      return image.width() + 'x' + image.height() + ' ' + at(8, 12) + ' ' + at(24, 12);
    })()
  )JS");

  // The size the canvas was laid out at, then the two samples. Spelled as one
  // string so that a failure prints everything at once: a snapshot of the
  // wrong size and a snapshot of the wrong colour are different bugs and the
  // first makes the second meaningless.
  EXPECT_EQ(result, std::string("32x24 0,255,0,255 255,0,0,255"));

  basalt::forgetSkiaCanvas(tag);
}
