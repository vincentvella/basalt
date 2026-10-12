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
#include "include/ports/SkFontMgr_mac_ct.h"
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
  // No `<Canvas>` mounts here yet, so there is no view to photograph. Null is
  // the answer the base class's `makeViewScreenshot` hands to the app, which
  // is better than an empty image: one says "not here" and the other says
  // "here, and blank".
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
  // The colour space is accepted and not yet honoured: a Display P3 surface
  // wants the GPU path, and this one is raster. An app asking for P3 gets
  // sRGB rather than nothing, which is the same trade core/TextDecorations.h
  // makes for a dotted underline.
  (void)useP3ColorSpace;
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  return SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
}

sk_sp<SkFontMgr> BasaltSkPlatformContext::createFontMgr() {
  return SkFontMgr_New_CoreText(nullptr);
}

} // namespace basalt
