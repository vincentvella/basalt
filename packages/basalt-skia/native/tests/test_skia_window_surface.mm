// Whether a `<Canvas>` can be drawn into without a window.
//
// 3.x took the drawing away from the host: `RNSkWindowSurface` owns the whole
// swapchain over `wgpu::Surface`, and its own comment says the platform view
// "only acquires the native handle and forwards its lifecycle (attach,
// resize, detach)". On Apple that handle is a `CAMetalLayer` -- and a
// CAMetalLayer is an ordinary layer that needs no window and no screen, which
// is the question this file exists to answer: if a layer off the hierarchy can
// be configured as a Dawn surface and presented onto, then the canvas this
// platform owes is testable here rather than only in an app.
//
// Objective-C++ for one reason, the layer. Everything else below is the
// portable half.

#include "TestHarness.h"

#include "BasaltSkPlatformContext.h"
#include "JsRuntimeAccess.h"

#import <QuartzCore/CAMetalLayer.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include "RNDawnContext.h"
#include "RNSkView.h"
#include "RNSkWindowSurface.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPictureRecorder.h"
#include "include/core/SkRect.h"
#pragma clang diagnostic pop

#include <memory>
#include <sstream>

namespace {

// The layer a canvas would own, at a size in pixels. Off the view hierarchy
// and off any screen: `drawableSize` is what the surface configures against,
// and nothing here asks the window server for anything.
CAMetalLayer *makeLayer(int width, int height) {
  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.frame = CGRectMake(0, 0, width, height);
  layer.drawableSize = CGSizeMake(width, height);
  return layer;
}

} // namespace

TEST(skia_a_window_surface_attaches_to_a_layer_with_no_window) {
  CAMetalLayer *layer = makeLayer(32, 24);
  auto surface = std::make_shared<RNSkia::RNSkWindowSurface>();
  EXPECT(!surface->isAttached());

  surface->setLayoutSize(32, 24);
  surface->attach((__bridge void *)layer, 32, 24, /* highBitDepth= */ false,
                  /* useP3ColorSpace= */ false, nullptr);

  // Attached means Dawn made a surface over the layer, which is the half a
  // host cannot do anything about: it is the `#ifdef __APPLE__` branch of
  // MakeWGPUSurface, and the reason GTK and Win32 need their own RNSkSurface.
  EXPECT(surface->isAttached());
  EXPECT_EQ(surface->getWidth(), 32);
  EXPECT_EQ(surface->getHeight(), 24);

  // And the target description a recorder needs, readable from any thread.
  RNSkia::RNSkGraphiteTargetInfo info;
  EXPECT(surface->getTargetInfo(&info));

  surface->detach();
  EXPECT(!surface->isAttached());
}

TEST(skia_a_view_binds_to_a_layer_and_survives_a_resize) {
  CAMetalLayer *layer = makeLayer(32, 24);
  auto context = std::make_shared<basalt::BasaltSkPlatformContext>(basalt::jsCallInvoker());
  auto surface = std::make_shared<RNSkia::RNSkWindowSurface>();
  surface->setLayoutSize(32, 24);
  surface->attach((__bridge void *)layer, 32, 24, false, false, nullptr);
  EXPECT(surface->isAttached());

  // The view, which is the piece a host builds and otherwise does not touch:
  // 3.x's RNSkView is concrete, where 2.x's was a base to subclass with a
  // canvas provider. Constructing it installs a redraw callback on the
  // surface, so this also pins that the two fit together.
  auto view = std::make_shared<RNSkia::RNSkView>(context, surface);
  view->setNativeId(7);
  EXPECT_EQ(static_cast<int>(view->getNativeId()), 7);

  // A redraw with nothing recorded, which is the state a canvas is in between
  // the layer appearing and the first picture arriving. It must do nothing
  // rather than present an unconfigured texture.
  view->redraw();
  RNSkia::RNSkGraphiteTargetInfo info;
  EXPECT(surface->getTargetInfo(&info));

  // Resized the way a layout change resizes it: reconfiguring the swapchain
  // is where this usually falls over, and it happens on every window resize.
  layer.drawableSize = CGSizeMake(64, 48);
  surface->resize(64, 48);
  EXPECT_EQ(surface->getWidth(), 64);
  EXPECT_EQ(surface->getHeight(), 48);
  EXPECT(surface->getTargetInfo(&info));
  view->redraw();

  // What is *not* asserted here, and why: presenting an actual frame needs a
  // picture, and a picture reaches a view only through
  // `RNSkView::setJsiProperties` with an `RNJsi::ViewProperty`, which
  // constructs from a jsi::Value and nothing else. So a frame on the layer is
  // a test that goes through the installed JavaScript API -- the next step,
  // with the view that owns this layer. This one answers the question that
  // decides whether that test can exist at all: a CAMetalLayer off the view
  // hierarchy, with no window and no screen, does become a Dawn surface.
  surface->detach();
  EXPECT(!surface->isAttached());
}
