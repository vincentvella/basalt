#include "Win32SkiaPeer.h"

#include "PlatformServices.h"
#include "RnWin32View.h"
#include "SkiaPictureViewComponent.h"
#include "Win32SkiaModule.h"

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkSurface.h"

#include "RNSkPictureView.h"
#include "RNSkView.h"

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace basalt {
namespace {

using Microsoft::WRL::ComPtr;

// The surface a `<Canvas>` renders into, and the thing that draws it.
//
// Two interfaces on one object because they are two views of one buffer: the
// package writes through `RNSkCanvasProvider` and the paint walk reads through
// `RnWin32Painter`, and an object between them would only mean keeping two
// pointers to the same pixels in step.
//
// Both halves run on the UI thread -- `renderToCanvas` arrives through the
// platform context's `runOnMainThread`, `draw` from WM_PAINT -- so the buffer
// needs no lock. That is checked rather than assumed; see renderToCanvas,
// which is also where the package's Metal provider checks it.
class Win32SkiaCanvas final : public RNSkia::RNSkCanvasProvider,
                              public win32::RnWin32Painter {
 public:
  Win32SkiaCanvas(std::function<void()> requestRedraw,
                  std::shared_ptr<RNSkia::RNSkPlatformContext> context,
                  std::function<void()> requestRepaint)
      : RNSkia::RNSkCanvasProvider(std::move(requestRedraw)),
        context_(std::move(context)),
        requestRepaint_(std::move(requestRepaint)) {}

  // Replaced on every mutation rather than kept from the first one. A canvas
  // that mounted before the host installed its repaint callback would
  // otherwise hold an empty function for as long as it lives, render every
  // frame perfectly and show none of them.
  void setRequestRepaint(std::function<void()> requestRepaint) {
    requestRepaint_ = std::move(requestRepaint);
  }

  // --- RNSkCanvasProvider ---------------------------------------------------
  //
  // "Scaled", in the package's vocabulary, means pixels rather than points:
  // the Metal provider multiplies by the pixel density in setSize and reports
  // the product here. Same here, so that `makeImageSnapshot` -- which builds
  // an offscreen provider out of these two numbers -- gets the resolution it
  // would have got on any other host.

  int getWidth() override { return pixelWidth_; }

  int getHeight() override { return pixelHeight_; }

  bool renderToCanvas(const std::function<void(SkCanvas *)> &callback) override {
    if (!isUiThread()) {
      // Off the UI thread, a paint may already be reading the buffer, and
      // drawing into it now would tear. Ask for a redraw and let it arrive on
      // the thread that owns the pixels -- the answer the package's Metal
      // provider gives when it finds itself off the main thread, and for the
      // same reason.
      _requestRedraw();
      return false;
    }
    if (surface_ == nullptr) {
      return false;
    }
    callback(surface_->getCanvas());
    // The pixels changed, so the Direct2D copy of them is stale and the window
    // is showing the frame before this one.
    generation_++;
    if (requestRepaint_) {
      requestRepaint_();
    }
    return true;
  }

  // In points, as the package's own providers take it: the density is applied
  // here rather than by the caller.
  void setSize(int width, int height) {
    const float density = context_->getPixelDensity();
    const int pixelWidth =
        std::max(0, static_cast<int>(std::lround(static_cast<float>(width) * density)));
    const int pixelHeight =
        std::max(0, static_cast<int>(std::lround(static_cast<float>(height) * density)));
    if (pixelWidth == pixelWidth_ && pixelHeight == pixelHeight_) {
      return;
    }
    pixelWidth_ = pixelWidth;
    pixelHeight_ = pixelHeight;
    surface_ = nullptr;
    // A device bitmap is made for one size; the next draw makes another.
    releaseDeviceBitmap();
    if (pixelWidth_ > 0 && pixelHeight_ > 0) {
      // N32Premul is premultiplied BGRA8888 on Windows, which is also the one
      // pixel format a Direct2D bitmap takes here. The copy in `draw` is
      // therefore a memcpy per row rather than a conversion -- worth stating,
      // because the day that stops being true it will show up as a quietly
      // halved frame rate and not as a compile error.
      surface_ = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(pixelWidth_, pixelHeight_));
      if (surface_ == nullptr) {
        LOG(WARNING) << "react-native-skia: could not make a " << pixelWidth_ << "x"
                     << pixelHeight_ << " surface for a <Canvas>";
      } else {
        // Cleared here and not only by the renderer. A raster surface is not
        // promised zeroed pixels, and there is a window between this and the
        // first picture -- a mount, a layout, a paint -- in which the canvas
        // is drawn. Uninitialised memory is a striking thing to put on screen.
        surface_->getCanvas()->clear(SK_ColorTRANSPARENT);
      }
    }
    // The picture drawn at the old size has to be drawn again at this one, and
    // nothing else would ask.
    _requestRedraw();
  }

  // --- RnWin32Painter -------------------------------------------------------

  void draw(ID2D1RenderTarget *target, float boxWidth, float boxHeight) override {
    if (surface_ == nullptr || target == nullptr || pixelWidth_ <= 0 || pixelHeight_ <= 0) {
      return;
    }
    if (boxWidth <= 0.0f || boxHeight <= 0.0f) {
      return;
    }
    if (!ensureDeviceBitmap(target)) {
      return;
    }
    // The destination is the view's box in points and the bitmap is in pixels,
    // which is where the density applied in setSize comes back out. On a
    // display at 1x the two are the same number and the draw is one to one.
    target->DrawBitmap(deviceBitmap_.Get(),
                       D2D1::RectF(0.0f, 0.0f, boxWidth, boxHeight),
                       1.0f,
                       D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
  }

 private:
  void releaseDeviceBitmap() {
    deviceBitmap_.Reset();
    deviceTarget_ = nullptr;
    deviceGeneration_ = 0;
  }

  // The Direct2D copy of the surface: made once, refilled per frame.
  //
  // Keyed on the target the way RnWin32Image's cache is, and for the same
  // reason -- a bitmap belongs to the target that made it, and this host draws
  // into a second one only for the offscreen snapshot. Refilled rather than
  // remade because `CopyFromMemory` reuses the allocation, which is the
  // difference between a copy per frame and a copy plus an allocation.
  bool ensureDeviceBitmap(ID2D1RenderTarget *target) {
    if (deviceBitmap_ == nullptr || deviceTarget_ != target) {
      deviceBitmap_.Reset();
      deviceTarget_ = nullptr;
      const D2D1_BITMAP_PROPERTIES properties = D2D1::BitmapProperties(
          D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
      ComPtr<ID2D1Bitmap> created;
      if (FAILED(target->CreateBitmap(
              D2D1::SizeU(static_cast<UINT32>(pixelWidth_), static_cast<UINT32>(pixelHeight_)),
              properties,
              created.GetAddressOf()))) {
        return false;
      }
      deviceBitmap_ = std::move(created);
      deviceTarget_ = target;
      // Nothing has been copied into it yet, whatever generation the surface
      // happens to be on.
      deviceGeneration_ = 0;
    }
    if (deviceGeneration_ == generation_) {
      return true;
    }
    SkPixmap pixels;
    if (!surface_->peekPixels(&pixels)) {
      // Only a GPU-backed surface cannot be peeked, and this one is raster by
      // construction. A line rather than a silent blank frame, for the day
      // that changes.
      LOG(WARNING) << "react-native-skia: a <Canvas> surface could not be read back";
      return false;
    }
    if (FAILED(deviceBitmap_->CopyFromMemory(
            nullptr, pixels.addr(), static_cast<UINT32>(pixels.rowBytes())))) {
      return false;
    }
    deviceGeneration_ = generation_;
    return true;
  }

  std::shared_ptr<RNSkia::RNSkPlatformContext> context_;
  std::function<void()> requestRepaint_;

  sk_sp<SkSurface> surface_;
  int pixelWidth_ = 0;
  int pixelHeight_ = 0;

  // Bumped by every render. The device bitmap remembers which generation it
  // holds, so a window that repaints for some other reason -- a resize, a
  // sibling changing -- redraws the canvas without copying its pixels again.
  uint64_t generation_ = 1;
  uint64_t deviceGeneration_ = 0;

  ComPtr<ID2D1Bitmap> deviceBitmap_;
  ID2D1RenderTarget *deviceTarget_ = nullptr;
};

// The package's view, holding the canvas above.
//
// `RNSkAppleView.h` is this in template form and cannot be included here: its
// first import is the Apple platform context, which is Objective-C++ over an
// RCTBridge. A `<Canvas>` needs exactly one RNSkView subclass, so this is that
// one rather than a template over the one.
class Win32SkiaPictureView final : public RNSkia::RNSkPictureView {
 public:
  Win32SkiaPictureView(const std::shared_ptr<RNSkia::RNSkPlatformContext> &context,
                       const std::function<void()> &requestRepaint)
      : RNSkia::RNSkPictureView(
            context,
            std::make_shared<Win32SkiaCanvas>(
                // Bound before the base class is constructed, exactly as the
                // package's own views bind it: nothing calls it until a
                // picture arrives, by which time this object is whole.
                std::bind(&RNSkia::RNSkView::requestRedraw, this),
                context,
                requestRepaint)) {}

  // `getCanvasProvider` is protected on RNSkView, which is why this is a
  // member rather than a cast at the call site.
  std::shared_ptr<Win32SkiaCanvas> canvas() {
    return std::static_pointer_cast<Win32SkiaCanvas>(getCanvasProvider());
  }
};

struct Attached {
  std::shared_ptr<Win32SkiaPictureView> view;
  // The id it was registered under, kept so the unregister matches: a view
  // whose nativeID changed would otherwise leave the old registration behind,
  // pointing at a surface nothing draws into.
  size_t nativeId = 0;
};

std::unordered_map<facebook::react::Tag, Attached> &attached() {
  static std::unordered_map<facebook::react::Tag, Attached> map;
  return map;
}

// `<Canvas>` stringifies a counter into nativeID. Anything else on a
// SkiaPictureView is not ours to interpret.
bool parseNativeId(const std::string &text, size_t &out) {
  if (text.empty()) {
    return false;
  }
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

} // namespace

bool applySkiaCanvas(win32::RnWin32View *view,
                     const facebook::react::ShadowView &shadowView,
                     const std::function<void()> &requestRepaint) {
  if (view == nullptr || shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) !=
          facebook::react::SkiaPictureViewComponentName) {
    return false;
  }

  const auto tag = shadowView.tag;
  // In points, which is what the canvas wants: it applies the pixel density
  // itself, and passing pixels would square it on a scaled display.
  const auto &frame = shadowView.layoutMetrics.frame;
  const int width = static_cast<int>(frame.size.width);
  const int height = static_cast<int>(frame.size.height);

  size_t nativeId = 0;
  if (!parseNativeId(view->nativeId(), nativeId)) {
    // Not an error: a SkiaPictureView can mount before its nativeID has been
    // applied, and the next updateView carries it.
    return true;
  }

  RNSkia::RNSkManager *manager = Win32SkiaModule::manager();
  if (manager == nullptr) {
    LOG(WARNING) << "SkiaPictureView tag " << tag
                 << " mounted before RNSkiaModule.install ran, so it has no canvas";
    return true;
  }

  auto &entry = attached()[tag];
  if (entry.view == nullptr) {
    entry.view =
        std::make_shared<Win32SkiaPictureView>(manager->getPlatformContext(), requestRepaint);
    entry.nativeId = nativeId;
    // The view draws it; this file only keeps it alive and in step.
    view->setPainter(entry.view->canvas());
    manager->setSkiaView(nativeId, entry.view);
    LOG(INFO) << "SkiaPictureView tag " << tag << " registered as Skia view " << nativeId;
  } else if (entry.nativeId != nativeId) {
    // The id moved. Re-register rather than leave the manager pointing the old
    // surface at the new id's pictures.
    manager->unregisterSkiaView(entry.nativeId);
    entry.nativeId = nativeId;
    manager->setSkiaView(nativeId, entry.view);
  }

  entry.view->canvas()->setRequestRepaint(requestRepaint);
  if (width > 0 && height > 0) {
    entry.view->canvas()->setSize(width, height);
  }
  return true;
}

void forgetSkiaCanvas(facebook::react::Tag tag) {
  auto it = attached().find(tag);
  if (it == attached().end()) {
    return;
  }
  if (RNSkia::RNSkManager *manager = Win32SkiaModule::manager()) {
    manager->unregisterSkiaView(it->second.nativeId);
  }
  attached().erase(it);
}

} // namespace basalt
