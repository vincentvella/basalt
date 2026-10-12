#include "BasaltSkiaCanvasWin32.h"

#include "BasaltSkiaModule.h"
#include "RnWin32View.h"
#include "SkiaViewComponent.h"
#include "Win32SkiaPeer.h"

#include <windows.h>

#include <d2d1.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"

#include "RNDawnContext.h"
#include "RNDawnUtils.h"
#include "RNSkManager.h"
#include "RNSkView.h"
#include "RNSkWindowSurface.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColorSpace.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Surface.h"

#pragma clang diagnostic pop

#include <glog/logging.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>

namespace basalt {

using Microsoft::WRL::ComPtr;

std::pair<int, int> skiaCanvasPixelSize(double widthInPoints,
                                        double heightInPoints,
                                        double density) {
  if (!(density > 0.0)) {
    // A scale of zero is what the platform context answers before a host has
    // reported anything, and multiplying by it turns a laid-out canvas into no
    // canvas. One point is one pixel until somebody says otherwise.
    density = 1.0;
  }
  return {static_cast<int>(std::lround(widthInPoints * density)),
          static_cast<int>(std::lround(heightInPoints * density))};
}

BasaltSkiaCanvas::BasaltSkiaCanvas(
    std::shared_ptr<RNSkia::RNSkPlatformContext> context,
    std::function<void()> requestRepaint)
    : context_(std::move(context)), requestRepaint_(std::move(requestRepaint)) {
}

BasaltSkiaCanvas::~BasaltSkiaCanvas() = default;

void BasaltSkiaCanvas::setRequestRepaint(std::function<void()> requestRepaint) {
  requestRepaint_ = std::move(requestRepaint);
}

SkSurface *BasaltSkiaCanvas::renderTarget(int width, int height,
                                          bool useP3ColorSpace) {
  if (width <= 0 || height <= 0) {
    return nullptr;
  }
  if (surface_ != nullptr && width == surfaceWidth_ &&
      height == surfaceHeight_ && useP3ColorSpace == surfaceUsesP3_) {
    return surface_.get();
  }
  // The color type the target description already promised. Not a choice made
  // here: RNSkGraphiteTarget records against DawnUtils::PreferedColorType
  // while a view has no surface, and a recording made for one format is
  // dropped rather than replayed onto another (RNSkGraphiteRecording::
  // hasFormatOf). Agreeing with that fallback is what makes the frames
  // recorded before the first layout presentable.
  const SkColorType colorType = DawnUtils::PreferedColorType;
  const SkImageInfo info = SkImageInfo::Make(
      width, height, colorType, kPremul_SkAlphaType,
      DawnUtils::viewColorSpace(useP3ColorSpace));
  auto *recorder = RNSkia::DawnContext::getInstance().getRecorder();
  sk_sp<SkSurface> surface = SkSurfaces::RenderTarget(recorder, info);
  if (surface == nullptr) {
    LOG(WARNING) << "basalt-skia: Graphite would not make a " << width << "x"
                 << height << " target for a <Canvas>";
    return nullptr;
  }
  surface_ = std::move(surface);
  surfaceWidth_ = width;
  surfaceHeight_ = height;
  surfaceUsesP3_ = useP3ColorSpace;
  // A new Graphite surface clears itself in its first render pass, which its
  // own recorder only records when it next snaps. The recordings a frame is
  // made of go straight to the context, so that clear would run after them
  // and wipe them: it is recorded and submitted here instead, where the
  // surface is new and nothing has been drawn yet. The explicit clear is what
  // makes sure the pass is emitted at all. RNSkOffscreenSurface does the same
  // thing a step later, for the same reason.
  surface_->getCanvas()->clear(SK_ColorTRANSPARENT);
  if (auto *surfaceRecorder = surface_->recorder()) {
    if (auto recording = surfaceRecorder->snap()) {
      RNSkia::DawnContext::getInstance().submitRecording(recording.get());
    }
  }
  return surface_.get();
}

std::optional<std::pair<int, int>>
BasaltSkiaCanvas::renderTargetSize() const {
  if (surface_ == nullptr) {
    return std::nullopt;
  }
  return std::make_pair(surfaceWidth_, surfaceHeight_);
}

void BasaltSkiaCanvas::releaseRenderTarget() {
  surface_ = nullptr;
  surfaceWidth_ = 0;
  surfaceHeight_ = 0;
}

void BasaltSkiaCanvas::deliverFrame(
    uint64_t sequence, std::unique_ptr<const SkImage::AsyncReadResult> result,
    int width, int height, SkColorType colorType) {
  if (result == nullptr) {
    // The read failed. Nothing to show and nothing to retry: the next frame
    // overwrites this one anyway.
    return;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sequence <= deliveredSequence_) {
      // Overtaken. Two readbacks were in flight and the newer one landed
      // first; showing this one would run the canvas backwards for a frame.
      return;
    }
    deliveredSequence_ = sequence;
    frame_ = std::move(result);
    frameWidth_ = width;
    frameHeight_ = height;
    frameColorType_ = colorType;
    generation_++;
  }
  // On the main thread and out of DawnContext's lock before the repaint runs:
  // this is called from the readback poller, which is holding that lock, and
  // every other view's frames are behind it.
  std::weak_ptr<BasaltSkiaCanvas> weakThis = weak_from_this();
  context_->runOnMainThread([weakThis]() {
    if (auto self = weakThis.lock()) {
      if (self->requestRepaint_) {
        self->requestRepaint_();
      }
    }
  });
}

uint64_t BasaltSkiaCanvas::deliveredFrames() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return generation_;
}

void BasaltSkiaCanvas::releaseDeviceBitmap() {
  deviceBitmap_.Reset();
  deviceTarget_ = nullptr;
  deviceWidth_ = 0;
  deviceHeight_ = 0;
}

bool BasaltSkiaCanvas::ensureDeviceBitmap(ID2D1RenderTarget *target, int width,
                                          int height, SkColorType colorType) {
  if (deviceBitmap_ != nullptr && deviceTarget_ == target &&
      deviceWidth_ == width && deviceHeight_ == height) {
    return true;
  }
  releaseDeviceBitmap();

  // The bitmap takes the readback's bytes as they are, in whichever order
  // Graphite read them: Direct2D accepts both orders. Not an assumption --
  // RGBA8 was measured on the WIC target the canvas test draws into, which is
  // the one that would be fussiest, and the alternative would be a per-pixel
  // channel swap on every frame.
  const DXGI_FORMAT format = colorType == kBGRA_8888_SkColorType
                                 ? DXGI_FORMAT_B8G8R8A8_UNORM
                                 : DXGI_FORMAT_R8G8B8A8_UNORM;
  ComPtr<ID2D1Bitmap> created;
  if (FAILED(target->CreateBitmap(
          D2D1::SizeU(static_cast<UINT32>(width),
                      static_cast<UINT32>(height)),
          D2D1::BitmapProperties(
              D2D1::PixelFormat(format, D2D1_ALPHA_MODE_PREMULTIPLIED)),
          created.GetAddressOf()))) {
    // A line rather than a silent blank canvas: this is the one step between
    // a frame that rendered correctly and a frame on screen.
    LOG(WARNING) << "basalt-skia: Direct2D would not make a " << width << "x"
                 << height << " bitmap for a <Canvas>";
    return false;
  }
  deviceBitmap_ = std::move(created);
  deviceTarget_ = target;
  deviceWidth_ = width;
  deviceHeight_ = height;
  // Nothing has been copied into it yet, whatever generation the frame
  // happens to be on.
  deviceGeneration_ = 0;
  return true;
}

void BasaltSkiaCanvas::draw(ID2D1RenderTarget *target, float boxWidth,
                            float boxHeight) {
  if (target == nullptr || boxWidth <= 0.0f || boxHeight <= 0.0f) {
    return;
  }

  // The frame under the lock, the pixels outside it: the readback thread is
  // inside DawnContext's lock when it hands one over, and the copy below is
  // measured in megabytes.
  std::shared_ptr<const SkImage::AsyncReadResult> frame;
  uint64_t generation = 0;
  int width = 0;
  int height = 0;
  SkColorType colorType = kUnknown_SkColorType;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    frame = frame_;
    generation = generation_;
    width = frameWidth_;
    height = frameHeight_;
    colorType = frameColorType_;
  }
  if (frame == nullptr || width <= 0 || height <= 0) {
    // No frame yet: the view is mounted and laid out and the first one is on
    // its way. Drawing nothing is right -- a canvas has no background of its
    // own, and the view behind it already painted.
    return;
  }
  if (!ensureDeviceBitmap(target, width, height, colorType)) {
    return;
  }
  if (deviceGeneration_ != generation) {
    // The one copy this arrangement costs, and the reason the frame is held
    // by generation rather than copied on arrival: a window that repaints for
    // some other reason draws the same bitmap again without paying it.
    if (FAILED(deviceBitmap_->CopyFromMemory(
            nullptr, frame->data(0),
            static_cast<UINT32>(frame->rowBytes(0))))) {
      return;
    }
    deviceGeneration_ = generation;
  }

  // The destination is the view's box in points and the bitmap is in pixels,
  // which is where the density applied by the peer comes back out. On a
  // display at 1x the two are the same number and the draw is one to one.
  target->DrawBitmap(deviceBitmap_.Get(),
                     D2D1::RectF(0.0f, 0.0f, boxWidth, boxHeight), 1.0f,
                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
}

namespace {

// Everything the peer keeps per mounted view. The canvas itself is held by the
// view as its painter as well, which is what keeps it alive while a readback
// is in flight.
//
// Declared in the order they are built and destroyed in the reverse of it,
// which is the order they depend on each other: the view holds the surface
// and the surface reaches into the canvas, so the canvas is the last to go.
struct Attached {
  std::shared_ptr<BasaltSkiaCanvas> canvas;
  std::shared_ptr<RNSkia::RNSkWindowSurface> surface;
  std::shared_ptr<RNSkia::RNSkView> view;
  // The id it was registered under, kept so the unregister matches: a view
  // whose nativeID changed would otherwise leave the old registration behind,
  // pointing at a canvas nothing draws into.
  size_t nativeId = 0;
  // Whether the surface has been attached, and at what size in pixels.
  // `RNSkWindowSurface::isAttached()` cannot answer on this host -- see the
  // note at the top of BasaltSkiaCanvasWin32.h -- and a layout that did not
  // change the pixel size must not remake the Graphite surface, which throws
  // the last frame away for nothing.
  bool attached = false;
  int width = 0;
  int height = 0;
};

std::unordered_map<facebook::react::Tag, Attached> &attached() {
  static std::unordered_map<facebook::react::Tag, Attached> map;
  return map;
}

// `<Canvas>` stringifies a counter into nativeID. Anything else on a SkiaView
// is not ours to interpret.
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

std::optional<std::pair<int, int>>
skiaCanvasAttachedSize(facebook::react::Tag tag) {
  const auto it = attached().find(tag);
  if (it == attached().end() || it->second.canvas == nullptr) {
    return std::nullopt;
  }
  return it->second.canvas->renderTargetSize();
}

uint64_t skiaCanvasFrameCount(facebook::react::Tag tag) {
  const auto it = attached().find(tag);
  if (it == attached().end() || it->second.canvas == nullptr) {
    return 0;
  }
  return it->second.canvas->deliveredFrames();
}

bool applySkiaCanvas(win32::RnWin32View *view,
                     const facebook::react::ShadowView &shadowView,
                     const std::function<void()> &requestRepaint) {
  if (view == nullptr || shadowView.componentName == nullptr ||
      std::string_view(shadowView.componentName) !=
          facebook::react::SkiaViewComponentName) {
    return false;
  }

  const auto tag = shadowView.tag;
  size_t nativeId = 0;
  if (!parseNativeId(view->nativeId(), nativeId)) {
    // Not an error: a SkiaView is mounted before its nativeID has been applied
    // in some orders, and the next updateView carries it.
    return true;
  }

  RNSkia::RNSkManager *manager = BasaltSkiaModule::manager();
  if (manager == nullptr) {
    LOG(WARNING) << "SkiaView tag " << tag
                 << " mounted before RNSkiaModule.install ran, so it has no "
                    "canvas";
    return true;
  }

  auto &entry = attached()[tag];
  if (entry.view == nullptr) {
    entry.canvas = std::make_shared<BasaltSkiaCanvas>(
        manager->getPlatformContext(), requestRepaint);
    entry.surface = std::make_shared<RNSkia::RNSkWindowSurface>();
    entry.view = std::make_shared<RNSkia::RNSkView>(
        manager->getPlatformContext(), entry.surface);
    entry.nativeId = nativeId;
    // The view draws it; this file only keeps it alive and in step.
    view->setPainter(entry.canvas);
    // No frame scheduler, which is upstream's macOS answer too: there is no
    // display link here, and RNSkView::scheduleFrame falls through to redraw()
    // without one. The redraw already arrives on the main thread through the
    // platform context, and the present after it is a submit rather than a
    // blocking wait on a swapchain.
    manager->registerSkiaView(nativeId, entry.view);
    LOG(INFO) << "SkiaView tag " << tag << " registered as Skia view "
              << nativeId;
  } else if (entry.nativeId != nativeId) {
    // The id moved. Re-register rather than leave the manager pointing the old
    // canvas at the new id's frames.
    manager->unregisterSkiaView(entry.nativeId);
    entry.nativeId = nativeId;
    entry.view->setNativeId(nativeId);
    manager->registerSkiaView(nativeId, entry.view);
  }

  entry.canvas->setRequestRepaint(requestRepaint);

  // In points from the shadow view, in pixels for the surface.
  const auto &frame = shadowView.layoutMetrics.frame;
  const double density = manager->getPlatformContext()->getPixelDensity();
  const auto [width, height] =
      skiaCanvasPixelSize(frame.size.width, frame.size.height, density);
  if (width <= 0 || height <= 0) {
    return true;
  }
  // The size the surface will have, which a recorder may ask for before the
  // surface exists. Always, not only on a change: it is the layout rather than
  // the target.
  entry.surface->setLayoutSize(width, height);
  if (!entry.attached) {
    // The canvas is owned here and by the view, so there is nothing for the
    // surface to release: no releaser. High bit depth is not offered -- the
    // frame is read back into an 8-bit Direct2D bitmap, so the precision would
    // be thrown away in the copy.
    entry.surface->attach(entry.canvas.get(), width, height,
                          /* highBitDepth= */ false,
                          manager->getPlatformContext()->prefersP3ColorSpace(),
                          nullptr);
    entry.attached = true;
    entry.width = width;
    entry.height = height;
  } else if (width != entry.width || height != entry.height) {
    entry.surface->resize(width, height);
    entry.width = width;
    entry.height = height;
  }
  return true;
}

void forgetSkiaCanvas(facebook::react::Tag tag) {
  auto it = attached().find(tag);
  if (it == attached().end()) {
    return;
  }
  Attached &entry = it->second;
  if (RNSkia::RNSkManager *manager = BasaltSkiaModule::manager()) {
    manager->unregisterSkiaView(entry.nativeId);
  }
  // The surface before the canvas: detaching drops the Graphite target, and a
  // canvas released with a readback still pointing at it is the one order that
  // matters. (The readback holds a weak_ptr, so it is safe either way; this is
  // the order that does not rely on that.)
  if (entry.surface != nullptr) {
    entry.surface->detach();
  }
  attached().erase(it);
}

} // namespace basalt
