// The AppKit half of a `<Canvas>` on Skia 3.x: a CAMetalLayer and its
// lifecycle, and nothing else.
//
// That really is all a host owes now. `RNSkWindowSurface` owns the whole
// swapchain over `wgpu::Surface` and says so in its own comment -- the
// platform view "only acquires the native handle and forwards its lifecycle
// (attach, resize, detach)". So where the 2.x peer beside this one built an
// `RNSkMetalCanvasProvider` and subclassed `RNSkView` to carry it, this makes
// a layer, hands it over, and lets Skia do the rest.
//
// A sublayer rather than a view of its own, exactly as the package does it and
// as the 2.x peer does: the Skia layer draws itself on the GPU and takes no
// part in layout, hit testing or the view tree. Making it a view would put it
// in all three.
//
// ## Why this file is in basalt-skia rather than in basalt-appkit
//
// Because it only compiles when the app installed a Skia that has Dawn, and a
// host package should not carry a file whose compilability depends on somebody
// else's node_modules. basalt-skia contributes it to
// `BASALT_PACKAGE_APPKIT_SOURCES`, which means it compiles inside the AppKit
// target and sees that target's headers -- `RnAppKitView.h` and
// `AppKitSkiaPeer.h` among them. The 2.x peer stays where it is: its
// dependency is `BASALT_SKIA`, which core's own cmake/Skia.cmake decides.
//
// The two share `applySkiaCanvas` and `forgetSkiaCanvas` and never exist
// together. `BASALT_HAS_SKIA` is set only where the Ganesh half was built, and
// cmake/Skia.cmake refuses a 3.x install by version, so exactly one definition
// of each is linked. AppKitMountingManager.mm calls them under a guard that
// covers either.
//
// ## Points and pixels
//
// 2.x took points: its canvas provider scaled by the layer's `contentsScale`
// itself. `RNSkWindowSurface::attach` takes *pixels* -- "at the given size in
// pixels" -- so the layout is multiplied by the pixel density here, and
// rounded rather than truncated, which is what upstream's own SkiaView.mm does
// and for the reason it gives: a product like 1169.9999 should give the pixel
// size the layout means.

#include "BasaltSkiaCanvasAppKit.h"

#include "AppKitSkiaPeer.h"

#import "RnAppKitView.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>

#include "BasaltSkiaModule.h"
#include "SkiaViewComponent.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include "RNSkManager.h"
#include "RNSkView.h"
#include "RNSkWindowSurface.h"
#pragma clang diagnostic pop

#include <glog/logging.h>

#include <cmath>
#include <memory>
#include <string>
#include <unordered_map>

namespace basalt {

std::pair<int, int> skiaCanvasPixelSize(double widthInPoints,
                                        double heightInPoints,
                                        double density) {
  if (!(density > 0.0)) {
    // A scale of zero is what `currentDeviceMetrics()` used to answer before a
    // host reported anything, and multiplying by it turns a laid-out canvas
    // into no canvas. One point is one pixel until somebody says otherwise.
    density = 1.0;
  }
  return {static_cast<int>(std::lround(widthInPoints * density)),
          static_cast<int>(std::lround(heightInPoints * density))};
}

namespace {

// Everything one canvas owns. The layer is retained by its superlayer while it
// is in the tree, and strongly here as well so that detaching the surface in
// the destructor still has one.
struct Canvas {
  CAMetalLayer *layer = nil;
  std::shared_ptr<RNSkia::RNSkWindowSurface> surface;
  std::shared_ptr<RNSkia::RNSkView> view;
  // The id it was registered under, kept so the unregister matches: a view
  // whose nativeID changed would otherwise leave the old registration behind,
  // pointing at a canvas nothing draws into.
  size_t nativeId = 0;
  // The size the surface was last attached at, in pixels. A layout that did
  // not change the pixel size must not reconfigure the swapchain, which is
  // work and a visible flicker on every unrelated prop change.
  int width = 0;
  int height = 0;
};

std::unordered_map<facebook::react::Tag, Canvas> &canvases() {
  static std::unordered_map<facebook::react::Tag, Canvas> map;
  return map;
}

// `<Canvas>` stringifies a counter into nativeID. Anything else on a SkiaView
// is not ours to interpret.
bool parseNativeId(NSString *identifier, size_t &out) {
  if (identifier == nil || identifier.length == 0) {
    return false;
  }
  const std::string text = identifier.UTF8String;
  try {
    size_t consumed = 0;
    const unsigned long long value = std::stoull(text, &consumed);
    if (consumed != text.size()) {
      return false;
    }
    out = static_cast<size_t>(value);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

// Whether the window can show a frame right now. Presenting into a hidden
// app's layer can clear it, leaving the canvas empty when the app comes back --
// upstream carries the same check and cites
// Shopify/react-native-skia#1257 for it. The surface requests a redraw instead,
// so the frame is not lost.
bool canPresentNow() {
  return [[NSThread currentThread] isMainThread] && !NSApplication.sharedApplication.isHidden;
}

} // namespace

bool applySkiaCanvas(RnAppKitView *view, const facebook::react::ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string(shadowView.componentName) != facebook::react::SkiaViewComponentName) {
    return false;
  }

  const auto tag = shadowView.tag;
  size_t nativeId = 0;
  if (!parseNativeId(view.rnNativeId, nativeId)) {
    // Not an error: a SkiaView is mounted before its nativeID has been applied
    // in some orders, and the next updateView carries it.
    return true;
  }

  RNSkia::RNSkManager *manager = BasaltSkiaModule::manager();
  if (manager == nullptr) {
    LOG(WARNING) << "SkiaView tag " << tag
                 << " mounted before RNSkiaModule.install ran, so it has no canvas";
    return true;
  }

  auto &canvas = canvases()[tag];
  if (canvas.view == nullptr) {
    canvas.layer = [CAMetalLayer layer];
    canvas.surface = std::make_shared<RNSkia::RNSkWindowSurface>();
    canvas.surface->setCanPresent(canPresentNow);
    canvas.view = std::make_shared<RNSkia::RNSkView>(manager->getPlatformContext(),
                                                     canvas.surface);
    canvas.nativeId = nativeId;
    // Under the view's own layer, and below nothing: a canvas has no React
    // children to sit above, and the package adds it the same way.
    [view.layer addSublayer:canvas.layer];
    // No frame scheduler, which is upstream's macOS answer too: iOS arms a
    // CADisplayLink and macOS presents as soon as the main thread gets to it.
    // RNSkView::scheduleFrame falls through to redraw() without one, and the
    // redraw already arrives on the main thread through the platform context.
    manager->registerSkiaView(nativeId, canvas.view);
    LOG(INFO) << "SkiaView tag " << tag << " registered as Skia view " << nativeId;
  } else if (canvas.nativeId != nativeId) {
    // The id moved. Re-register rather than leave the manager pointing the old
    // canvas at the new id's frames.
    manager->unregisterSkiaView(canvas.nativeId);
    canvas.nativeId = nativeId;
    canvas.view->setNativeId(nativeId);
    manager->registerSkiaView(nativeId, canvas.view);
  }

  // In points from the shadow view, in pixels for the surface.
  const auto &frame = shadowView.layoutMetrics.frame;
  const double density = manager->getPlatformContext()->getPixelDensity();
  const auto [width, height] =
      skiaCanvasPixelSize(frame.size.width, frame.size.height, density);
  if (width <= 0 || height <= 0) {
    return true;
  }

  // The layer's own frame is in points, like every other layer's: AppKit lays
  // it out in the superlayer's coordinate space and the contents scale makes
  // the pixels. Inside a transaction with actions disabled, so a resize does
  // not animate the layer to its new bounds while the surface is already
  // drawing at the new size.
  [CATransaction begin];
  [CATransaction setDisableActions:YES];
  canvas.layer.frame = CGRectMake(0, 0, frame.size.width, frame.size.height);
  canvas.layer.contentsScale = density;
  canvas.layer.drawableSize = CGSizeMake(width, height);
  // The size the surface will have, which a recorder may ask for before the
  // surface exists. Always, not only on a change: it is the layout rather than
  // the swapchain.
  canvas.surface->setLayoutSize(width, height);
  if (!canvas.surface->isAttached()) {
    // The layer is owned here and by its superlayer, so there is nothing for
    // the surface to release: no releaser.
    canvas.surface->attach((__bridge void *)canvas.layer, width, height,
                           /* highBitDepth= */ false,
                           manager->getPlatformContext()->prefersP3ColorSpace(), nullptr);
    canvas.width = width;
    canvas.height = height;
  } else if (width != canvas.width || height != canvas.height) {
    canvas.surface->resize(width, height);
    canvas.width = width;
    canvas.height = height;
  }
  [CATransaction commit];
  return true;
}

std::optional<std::pair<int, int>> skiaCanvasAttachedSize(facebook::react::Tag tag) {
  const auto it = canvases().find(tag);
  if (it == canvases().end() || it->second.surface == nullptr ||
      !it->second.surface->isAttached()) {
    return std::nullopt;
  }
  return std::make_pair(it->second.surface->getWidth(), it->second.surface->getHeight());
}

void forgetSkiaCanvas(facebook::react::Tag tag) {
  auto it = canvases().find(tag);
  if (it == canvases().end()) {
    return;
  }
  Canvas &canvas = it->second;
  if (RNSkia::RNSkManager *manager = BasaltSkiaModule::manager()) {
    manager->unregisterSkiaView(canvas.nativeId);
  }
  // The surface before the layer: detaching destroys the Dawn surface over it,
  // and a layer taken out of the tree with a live swapchain still on it is the
  // one order that leaves Dawn holding a handle to nothing.
  if (canvas.surface != nullptr) {
    canvas.surface->detach();
  }
  [canvas.layer removeFromSuperlayer];
  canvases().erase(it);
}

} // namespace basalt
