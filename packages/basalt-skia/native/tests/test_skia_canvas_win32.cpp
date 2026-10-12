// The Win32 canvas: what a `<Canvas>` on Skia 3.x gets when it mounts here,
// and what reaches the pixels.
//
// This host is the one that can test the whole way through. The AppKit canvas
// presents to a CAMetalLayer and there is nothing to read back off a swapchain
// texture that has been presented, so `test_skia_canvas.mm` stops at
// `makeImageSnapshot` -- an offscreen replay, not the frame the compositor
// got. Win32 presents into the Direct2D walk, and that walk renders into a WIC
// bitmap with no window and no display. So the last test in this file asks the
// question the other host cannot: a picture goes in through JavaScript and the
// pixels come out of the paint walk.
//
// What is asserted besides: whether this component is the canvas's business,
// whether there is an id yet, the pixel arithmetic, the surface at the right
// size, a resize that does not remake it for nothing, an id that moved, and
// the teardown.

#include "TestHarness.h"

#include "BasaltSkiaCanvasWin32.h"
#include "BasaltSkiaModule.h"
#include "JsRuntimeAccess.h"
#include "RnWin32View.h"
#include "SkiaPictureViewComponent.h"
#include "SkiaViewComponent.h"
#include "Win32SkiaPeer.h"
#include "Win32Snapshot.h"
#include "Win32UiThread.h"

#include <windows.h>

#include <hermes/hermes.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include "RNSkManager.h"
#pragma clang diagnostic pop

#include <ReactCommon/TurboModule.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/mounting/ShadowView.h>

#include <chrono>
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>

using basalt::win32::RnPixel;
using basalt::win32::RnPixels;
using basalt::win32::RnWin32View;
using facebook::react::LayoutMetrics;
using facebook::react::ShadowView;
using facebook::react::Tag;
using facebook::react::ViewProps;

namespace {

// Rounding through premultiplied storage costs a unit or two, and Direct2D
// antialiases edges -- so every point asked about below is well inside a half
// rather than on the boundary. The same tolerance test_win32_paint.cpp uses.
constexpr double kTolerance = 3.0;

ShadowView makeShadowView(const char *componentName, Tag tag, float width,
                          float height) {
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

// The module, installed once for the whole file. A canvas without a manager
// does nothing and says so, which is its own case below; everything after it
// needs Skia's bindings to exist.
struct Installed {
  std::unique_ptr<facebook::jsi::Runtime> runtime;
  std::shared_ptr<facebook::react::TurboModule> module;
};

Installed &installed() {
  static Installed state = [] {
    Installed fresh;
    fresh.runtime = facebook::hermes::makeHermesRuntime();
    fresh.module = basalt::makeSkiaTurboModule(
        basalt::BasaltSkiaModule::kModuleName, basalt::jsCallInvoker());
    if (fresh.runtime != nullptr && fresh.module != nullptr) {
      facebook::jsi::Value method = fresh.module->get(
          *fresh.runtime,
          facebook::jsi::PropNameID::forAscii(*fresh.runtime, "install"));
      if (method.isObject() &&
          method.getObject(*fresh.runtime).isFunction(*fresh.runtime)) {
        method.getObject(*fresh.runtime)
            .getFunction(*fresh.runtime)
            .call(*fresh.runtime);
      }
    }
    return fresh;
  }();
  return state;
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
        std::make_shared<facebook::jsi::StringBuffer>(source),
        "test_skia_canvas_win32.js");
    return value.toString(*state.runtime).utf8(*state.runtime);
  } catch (const facebook::jsi::JSError &error) {
    return std::string("JSError: ") + error.getMessage();
  } catch (const std::exception &error) {
    return std::string("exception: ") + error.what();
  }
}

// A real message loop for the duration of one test, and the inline posting
// the rest of the suite expects put back afterwards.
//
// The canvas is the one thing in this host that needs a loop. A frame is
// recorded on react-native-skia's own thread pool, and the producer asks the
// UI thread to present it while holding its lock. With no loop installed
// `postToUiThread` runs the work inline -- which on a worker means *on the
// worker*, the hazard Win32UiThread.h names -- and the present re-enters the
// producer on the thread that already holds that lock. MSVC's std::mutex
// throws on the recursive lock, nothing on a thread-pool thread catches it,
// and the process dies with no message at all. An app never sees this:
// PostMessage, dispatch_async and g_idle_add all queue.
struct ScopedUiThread {
  ScopedUiThread() { basalt::installUiThread(); }
  ~ScopedUiThread() { basalt::uninstallUiThread(); }
  ScopedUiThread(const ScopedUiThread &) = delete;
  ScopedUiThread &operator=(const ScopedUiThread &) = delete;
};

// Pumps that loop until `ready`, or until the deadline. Returns whether it
// became true.
//
// A poll rather than a wait because two things have to happen and only one of
// them posts: the readback completes on DawnContext's own thread, and the
// repaint it asks for arrives here. A second is far longer than a 32x24
// round trip takes and short enough that a hang is a failure rather than a
// hung suite.
bool pumpUntil(const std::function<bool()> &ready) {
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (std::chrono::steady_clock::now() < deadline) {
    MSG message;
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
      TranslateMessage(&message);
      DispatchMessageW(&message);
    }
    if (ready()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

// What the mounting manager hands over as "the screen is stale". Counted
// rather than ignored: a frame that renders and never asks for a repaint is a
// canvas that works and shows nothing, which is the failure this callback
// exists to prevent.
int gRepaints = 0;

} // namespace

#define EXPECT_PIXEL(pixels, x, y, r, g, b, a)                                 \
  do {                                                                         \
    const RnPixel pixel = (pixels).at((x), (y));                               \
    EXPECT_NEAR(pixel.red, (r), kTolerance);                                   \
    EXPECT_NEAR(pixel.green, (g), kTolerance);                                 \
    EXPECT_NEAR(pixel.blue, (b), kTolerance);                                  \
    EXPECT_NEAR(pixel.alpha, (a), kTolerance);                                 \
  } while (false)

TEST(win32_skia_canvas_is_not_interested_in_other_components) {
  // A filter rather than a question asked twice: the mounting manager calls
  // this for every view it touches, and false is how it learns to carry on.
  RnWin32View view(10);
  view.setNativeId("1");
  EXPECT(!basalt::applySkiaCanvas(&view, makeShadowView("View", 10, 100, 50),
                                  nullptr));
  // Including the *other* Skia canvas. On a 3.x build a SkiaPictureView is a
  // 2.x component that nothing here draws, and claiming it would mean
  // reporting a canvas that never gets a picture.
  EXPECT(!basalt::applySkiaCanvas(
      &view,
      makeShadowView(facebook::react::SkiaPictureViewComponentName, 11, 100,
                     50),
      nullptr));
  EXPECT(view.painter() == nullptr);
}

TEST(win32_skia_canvas_waits_for_the_native_id) {
  // A SkiaView is mounted before its nativeID has been applied in some orders,
  // and the next updateView carries it. Claimed -- it is this component -- and
  // nothing built yet.
  EXPECT(installed().runtime != nullptr);
  RnWin32View view(20);
  EXPECT(basalt::applySkiaCanvas(
      &view, makeShadowView(facebook::react::SkiaViewComponentName, 20, 100, 50),
      nullptr));
  EXPECT(view.painter() == nullptr);

  // And a nativeID that is not a number is not an id. `<Canvas>` stringifies a
  // counter; anything else on a SkiaView is somebody else's prop.
  RnWin32View named(21);
  named.setNativeId("not-a-number");
  EXPECT(basalt::applySkiaCanvas(
      &named,
      makeShadowView(facebook::react::SkiaViewComponentName, 21, 100, 50),
      nullptr));
  EXPECT(named.painter() == nullptr);
}

TEST(win32_skia_canvas_sizes_its_surface_in_pixels) {
  // Separate from the peer because the peer cannot be asked this: the scale is
  // 1 on a machine with no host reporting one, and at scale 1 a peer that
  // multiplied and a peer that forgot produce the same number.
  EXPECT(basalt::skiaCanvasPixelSize(100, 50, 2.0) == std::make_pair(200, 100));
  EXPECT(basalt::skiaCanvasPixelSize(100, 50, 1.0) == std::make_pair(100, 50));
  // Rounded, not truncated. A layout of 584.9999 points at 2x is 1170 pixels
  // and not 1169, which is the case upstream's own SkiaView.mm calls out.
  EXPECT(basalt::skiaCanvasPixelSize(584.9999, 584.9999, 2.0) ==
         std::make_pair(1170, 1170));
  // A density nobody reported yet. Multiplying by zero would turn a laid-out
  // canvas into no canvas at all, which reads as a canvas that never mounted.
  EXPECT(basalt::skiaCanvasPixelSize(100, 50, 0.0) == std::make_pair(100, 50));
  // And a layout that has no size, which Yoga hands out before the first real
  // one. The peer renders nothing for these.
  EXPECT(basalt::skiaCanvasPixelSize(0, 0, 2.0) == std::make_pair(0, 0));
}

TEST(win32_skia_canvas_gives_the_view_a_painter_at_the_pixel_size) {
  EXPECT(installed().runtime != nullptr);
  EXPECT(basalt::BasaltSkiaModule::manager() != nullptr);

  const Tag tag = 30;
  RnWin32View view(tag);
  view.setNativeId("3");
  EXPECT(basalt::applySkiaCanvas(
      &view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 100, 50),
      nullptr));

  // A painter, which is how this host draws a canvas: there is no layer tree
  // to put one in, so the Direct2D walk asks the view for its content.
  EXPECT(view.painter() != nullptr);
  // And a Graphite surface behind it, at the pixel size. Not readable off
  // anything the host owns, which is why the peer answers.
  const auto size = basalt::skiaCanvasAttachedSize(tag);
  EXPECT(size.has_value());
  if (size.has_value()) {
    EXPECT_EQ(size->first, 100);
    EXPECT_EQ(size->second, 50);
  }

  // A zero-sized layout, which Yoga hands out before the first real one,
  // leaves what is there alone rather than remaking the surface at nothing.
  EXPECT(basalt::applySkiaCanvas(
      &view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 0, 0),
      nullptr));
  EXPECT(basalt::skiaCanvasAttachedSize(tag) == std::make_pair(100, 50));

  // A resize goes through.
  EXPECT(basalt::applySkiaCanvas(
      &view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 64, 64),
      nullptr));
  EXPECT(basalt::skiaCanvasAttachedSize(tag) == std::make_pair(64, 64));

  basalt::forgetSkiaCanvas(tag);
  EXPECT(!basalt::skiaCanvasAttachedSize(tag).has_value());
}

TEST(win32_skia_canvas_re_registers_an_id_that_moved) {
  EXPECT(installed().runtime != nullptr);
  RNSkia::RNSkManager *manager = basalt::BasaltSkiaModule::manager();
  EXPECT(manager != nullptr);
  if (manager == nullptr) {
    return;
  }

  const Tag tag = 40;
  RnWin32View view(tag);
  view.setNativeId("40");
  EXPECT(basalt::applySkiaCanvas(
      &view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 20, 20),
      nullptr));
  const std::shared_ptr<basalt::win32::RnWin32Painter> first = view.painter();
  EXPECT(first != nullptr);

  // The same view with a different nativeID. The canvas stays -- it is the
  // same view and the same surface -- and the manager is told, so that frames
  // for the new id reach it rather than nothing.
  view.setNativeId("41");
  EXPECT(basalt::applySkiaCanvas(
      &view, makeShadowView(facebook::react::SkiaViewComponentName, tag, 20, 20),
      nullptr));
  EXPECT(view.painter() == first);

  basalt::forgetSkiaCanvas(tag);
}

TEST(win32_skia_canvas_draws_a_picture_into_the_paint_walk) {
  // The claim nothing else here makes: a `<Canvas>` on 3.x *draws*, all the
  // way to the pixels a window would show. Everything above is about mounting
  // -- a painter on the view, a surface at the right size -- and a canvas that
  // mounts and shows nothing looks identical from the outside, which is the
  // trap docs/TESTING.md spends a section on.
  //
  // A picture reaches a view only through `RNSkView::setJsiProperties` with an
  // `RNJsi::ViewProperty`, which constructs from a `jsi::Value` and nothing
  // else -- so this goes the way an app goes: `SkiaApi` builds the picture in
  // JavaScript and `SkiaViewApi.setJsiProperty` pushes it to the view by id.
  // From there it is this host's own path: a Graphite recording replayed onto
  // an offscreen target, read back off the GPU, copied into a Direct2D bitmap
  // and drawn by the same walk that draws every other view.
  EXPECT(installed().runtime != nullptr);
  EXPECT(basalt::BasaltSkiaModule::manager() != nullptr);
  EXPECT_EQ(evaluate("typeof SkiaApi"), std::string("object"));
  EXPECT_EQ(evaluate("typeof SkiaViewApi"), std::string("object"));

  const ScopedUiThread uiThread;

  const Tag tag = 50;
  RnWin32View root(1);
  root.setFrame(0, 0, 32, 24);
  RnWin32View canvasView(tag);
  canvasView.setFrame(0, 0, 32, 24);
  canvasView.setNativeId("50");
  root.insertChild(&canvasView, 0);

  gRepaints = 0;
  EXPECT(basalt::applySkiaCanvas(
      &canvasView,
      makeShadowView(facebook::react::SkiaViewComponentName, tag, 32, 24),
      []() { gRepaints++; }));
  EXPECT(basalt::skiaCanvasAttachedSize(tag) == std::make_pair(32, 24));

  // Green over the left half of a 32x24 canvas, red over the right: two
  // rectangles rather than one fill, so that "drew something" and "drew it
  // where it was asked to" are separate claims. The pair e2e/skia.tsx uses.
  //
  // Red is also what catches the other mistake this path can make. Direct2D's
  // byte order is BGRA and Graphite reads back RGBA here, so a copy that got
  // the channels wrong turns red into blue -- and leaves green, whose channel
  // is in the middle either way, looking perfectly correct.
  const std::string pushed = evaluate(R"JS(
    (function () {
      var green = SkiaApi.Paint();
      green.setColor(SkiaApi.Color('rgb(0, 255, 0)'));
      var red = SkiaApi.Paint();
      red.setColor(SkiaApi.Color('rgb(255, 0, 0)'));

      var recorder = SkiaApi.PictureRecorder();
      var canvas = recorder.beginRecording(SkiaApi.XYWHRect(0, 0, 32, 24));
      canvas.drawRect(SkiaApi.XYWHRect(0, 0, 16, 24), green);
      canvas.drawRect(SkiaApi.XYWHRect(16, 0, 16, 24), red);
      SkiaViewApi.setJsiProperty(50, 'picture', recorder.finishRecordingAsPicture());
      return 'pushed';
    })()
  )JS");
  EXPECT_EQ(pushed, std::string("pushed"));

  // The whole path, waited for rather than assumed: the picture is recorded
  // on the thread pool, presented on this thread, read back on DawnContext's
  // thread and announced back here. Waiting on the repaint rather than on the
  // frame count is waiting on the last link of that chain.
  //
  // This is also the assertion that the path ran at all. Without it the
  // samples below would be empty for a reason no sample could distinguish
  // from a wrong colour.
  EXPECT(pumpUntil([]() { return gRepaints > 0; }));
  EXPECT(basalt::skiaCanvasFrameCount(tag) >= 1);

  const RnPixels pixels = basalt::win32::renderToPixels(root);
  EXPECT(!pixels.empty());
  EXPECT_PIXEL(pixels, 8, 12, 0, 255, 0, 255);
  EXPECT_PIXEL(pixels, 24, 12, 255, 0, 0, 255);

  root.removeChild(&canvasView);
  basalt::forgetSkiaCanvas(tag);
}
