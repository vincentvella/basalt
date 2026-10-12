#include "BasaltSkPlatformContext.h"

#include "DeviceMetrics.h"
#include "ImageBytes.h"
#include "PlatformServices.h"

#include <glog/logging.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"
#include "include/core/SkData.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"

// The system font manager, which is the one thing in this file that is not
// the same call on both platforms. Skia ships a backend per platform and
// each has its own header; the rest of what this context does is portable
// because Dawn made it so.
#ifdef __APPLE__
#include "include/ports/SkFontMgr_mac_ct.h"
#elif defined(_WIN32)
#include "include/ports/SkTypeface_win.h"
#endif

// Graphite, where the installed Skia has it.
//
// The definition comes from basalt-skia's own CMakeLists, which sets it when it
// found `react-native-webgpu-dawn` -- a dependency only a 3.x install has. So
// the condition is the renderer rather than a version number plumbed here to
// say the same thing, and 2.x, which is Ganesh, has no DawnContext at all.
#if BASALT_HAS_SKIA_GRAPHITE
#include "RNDawnContext.h"
#endif
#pragma clang diagnostic pop

#include <thread>
#include <utility>

namespace basalt {
namespace {

// The display's scale, which Skia keeps as a pixel density and uses to size
// what it rasterises. Asked of the same seam `Dimensions` answers from, so a
// Skia surface and a React Native layout agree about what a point is.
float devicePixelRatio() {
  const DeviceMetrics metrics = currentDeviceMetrics();
  const double scale = metrics.window.scale > 0 ? metrics.window.scale : 1.0;
  return static_cast<float>(scale);
}

} // namespace

BasaltSkPlatformContext::BasaltSkPlatformContext(
    std::shared_ptr<facebook::react::CallInvoker> callInvoker)
    : RNSkia::RNSkPlatformContext(std::move(callInvoker), devicePixelRatio()) {}

void BasaltSkPlatformContext::runOnMainThread(std::function<void()> func) {
  postToUiThread(std::move(func));
}

sk_sp<SkImage> BasaltSkPlatformContext::takeScreenshotFromViewTag(size_t tag) {
  // Still null, and no longer for want of a view: a `<Canvas>` does mount on
  // Win32 now. What is missing is the lookup. This takes a *React tag*, and
  // the map from one of those to an `RNSkView` belongs to whichever host's
  // canvas was compiled -- `appkit/BasaltSkiaCanvasAppKit.mm` or
  // `win32/BasaltSkiaCanvasWin32.cpp` -- while this file is the half both
  // share. Giving it one means a seam each host fills, and this is not the
  // API a `<Canvas>` draws through: `SkiaViewApi.makeImageSnapshot` goes
  // straight to the view by nativeId and works today. See
  // backlog/compatibility.md.
  //
  // Null rather than an empty image either way: one says "not here" and the
  // other says "here, and blank".
  (void)tag;
  return nullptr;
}

void BasaltSkPlatformContext::performStreamOperation(
    const std::string &sourceUri,
    const std::function<void(std::unique_ptr<SkStreamAsset>)> &op) {
  // Detached, which is what the Apple implementation does and for the same
  // reason: `fetchImageBytes` blocks, the caller is often the JavaScript
  // thread, and Skia's contract here is that `op` arrives whenever it arrives.
  std::thread([sourceUri, op]() {
    std::string bytes;
    std::string error;
    if (!fetchImageBytes(sourceUri, &bytes, &error)) {
      LOG(WARNING) << "skia: could not read " << sourceUri << ": " << error;
      // The callback is still called, with nothing: Skia's callers check for
      // a null stream, and one that is never called is a promise that never
      // settles.
      op(nullptr);
      return;
    }
    sk_sp<SkData> data = SkData::MakeWithCopy(bytes.data(), bytes.size());
    op(SkMemoryStream::Make(std::move(data)));
  }).detach();
}

void BasaltSkPlatformContext::raiseError(const std::exception &err) {
  LOG(ERROR) << "skia: " << err.what();
}

sk_sp<SkSurface> BasaltSkPlatformContext::makeOffscreenSurface(int width, int height,
                                                               bool useP3ColorSpace) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
#if BASALT_HAS_SKIA_GRAPHITE
  // Graphite's own, which is what upstream's Apple context answers with, and
  // on 3.x it is not an optimisation: a recording is replayed into a Graphite
  // target, so a raster surface here would be the wrong kind of surface rather
  // than a slower one. `drawAsImage` is the path that reaches this.
  return RNSkia::DawnContext::getInstance().MakeOffscreen(width, height,
                                                          useP3ColorSpace);
#else
  // 2.x, where this platform has no GPU context of its own yet. The colour
  // space is accepted and not honoured: a Display P3 surface wants the GPU
  // path, and an app asking for P3 gets sRGB rather than nothing, which is the
  // same trade core/TextDecorations.h makes for a dotted underline.
  (void)useP3ColorSpace;
  return SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
#endif
}

sk_sp<SkFontMgr> BasaltSkPlatformContext::createFontMgr() {
#ifdef __APPLE__
  return SkFontMgr_New_CoreText(nullptr);
#elif defined(_WIN32)
  // A null factory asks Skia for the process-wide one, which is what
  // CoreText does above and what the Win32 host already draws its own text
  // through -- so a typeface this returns and a typeface DirectWriteLayout
  // measured come from the same place.
  return SkFontMgr_New_DirectWrite();
#else
#error "basalt-skia: no system font manager for this platform"
#endif
}

} // namespace basalt
