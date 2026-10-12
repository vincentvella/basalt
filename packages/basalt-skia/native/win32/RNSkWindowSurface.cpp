// basalt's RNSkWindowSurface for Win32: upstream's class, different bodies.
//
// This file replaces `cpp/rnskia/RNSkWindowSurface.cpp` in the build on this
// host and nothing else -- the header above it is still react-native-skia's,
// unchanged and unpatched.
//
// ## Why a replacement and not a second RNSkSurface
//
// `RNSkSurface` is a five-method abstract interface and a window surface looks
// like the obvious thing to subclass. It is not available: `RNSkView::_surface`
// and `RNSkGraphiteTarget::_surface` are both `shared_ptr<RNSkWindowSurface>`
// by concrete type, and `RNSkSurface&` appears only as a parameter. A host
// that presents differently therefore changes the bodies of that class rather
// than substituting another, which is what this file is.
//
// ## What it does differently
//
// Upstream configures the native handle as a Dawn swapchain and presents to
// it. Win32 has no window to configure: this host owns one HWND and paints
// every view into it with Direct2D, so a `<Canvas>` renders into an offscreen
// Graphite target and the Direct2D walk draws the result. See
// `BasaltSkiaCanvasWin32.h` for the whole argument.
//
// So `MakeWGPUSurface` is never called here, `_surface` stays null and
// `configureSurface()` makes a Graphite render target rather than a
// swapchain. `presentRecordings` and `presentImage` replay onto that target
// and ask for a readback instead of presenting; the pixels reach the screen
// through the painter.
//
// ## Where the state lives
//
// In the object `attach` was given, cast back from `_nativeHandle`: upstream's
// header has members for a `wgpu::Surface` and the things around it and
// nowhere to put an `sk_sp<SkSurface>`, and the header is not ours to change.
// `BasaltSkiaCanvas` holds it, which is also the object the Direct2D walk
// draws -- the two halves of a frame on one object, which is what the 2.x peer
// settled on for the same reason.
//
// The members that *are* in the header are used as upstream uses them, so that
// the inline accessors on it still answer correctly: `_width`/`_height`, the
// `_targetInfo` triple behind `_targetInfoMutex`, `_layoutWidth`/`_layoutHeight`
// and both callbacks. The exception is `isAttached()`, which is inline, reads
// the swapchain that never exists here and is therefore always false. Nothing
// in the package calls it -- only a platform peer does, and this host's peer
// asks the canvas instead.

#include "RNSkWindowSurface.h"

#include "BasaltSkiaCanvasWin32.h"

#include "RNDawnContext.h"
#include "RNDawnUtils.h"
#include "utils/RNSkLog.h"

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"

#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Surface.h"
#include "include/gpu/graphite/dawn/DawnGraphiteTypes.h"
#include "include/gpu/graphite/dawn/DawnTypes.h"

#pragma clang diagnostic pop

#include <memory>
#include <utility>

namespace RNSkia {

namespace {

basalt::BasaltSkiaCanvas *canvasOf(void *nativeHandle) {
  return static_cast<basalt::BasaltSkiaCanvas *>(nativeHandle);
}

} // namespace

RNSkWindowSurface::~RNSkWindowSurface() { detach(); }

void RNSkWindowSurface::attach(void *nativeHandle, int width, int height,
                               bool highBitDepth, bool useP3ColorSpace,
                               Releaser releaser) {
  detach();
  if (nativeHandle == nullptr) {
    return;
  }
  auto &context = DawnContext::getInstance();
  _device = context.getWGPUDevice();
  _recorder = context.getRecorder();
  _nativeHandle = nativeHandle;
  _releaser = std::move(releaser);
  _width = width;
  _height = height;
  _useP3ColorSpace = useP3ColorSpace;
  if (highBitDepth) {
    // Not offered here, and said rather than silently granted: the frame is
    // read back into an 8-bit Direct2D bitmap, so the extra precision would be
    // thrown away in the copy that puts it on screen. A GPU path from the
    // Graphite texture into Direct2D would make this worth revisiting.
    RNSkLogger::logToConsole(
        "High bit depth was requested but this host presents through an "
        "8-bit Direct2D bitmap, falling back to the 8-bit format");
  }
  _format = DawnUtils::PreferredTextureFormat;
  _colorType = DawnUtils::PreferedColorType;
  // The usage a recording made before the surface existed assumes, which is
  // also what Graphite gives its own render targets. Agreeing with it is what
  // lets those frames be replayed rather than dropped; see
  // RNSkGraphiteTarget::resolveTargetInfo.
  _usage = DawnUtils::DefaultTargetUsage;
  configureSurface();
  updateTargetInfo();
  requestRedraw();
}

void RNSkWindowSurface::resize(int width, int height) {
  if (_nativeHandle == nullptr) {
    return;
  }
  _width = width;
  _height = height;
  configureSurface();
  updateTargetInfo();
  requestRedraw();
}

void RNSkWindowSurface::detach() {
  // The Graphite target goes before the canvas it belongs to.
  if (_nativeHandle != nullptr) {
    canvasOf(_nativeHandle)->releaseRenderTarget();
  }
  _recorder = nullptr;
  updateTargetInfo();
  if (_releaser && _nativeHandle != nullptr) {
    _releaser(_nativeHandle);
  }
  _releaser = nullptr;
  _nativeHandle = nullptr;
}

void RNSkWindowSurface::setLayoutSize(int width, int height) {
  std::lock_guard<std::mutex> lock(_targetInfoMutex);
  _layoutWidth = width;
  _layoutHeight = height;
}

bool RNSkWindowSurface::getLayoutSize(int *width, int *height) {
  std::lock_guard<std::mutex> lock(_targetInfoMutex);
  if (_layoutWidth <= 0 || _layoutHeight <= 0) {
    return false;
  }
  *width = _layoutWidth;
  *height = _layoutHeight;
  return true;
}

int RNSkWindowSurface::getWidth() {
  std::lock_guard<std::mutex> lock(_targetInfoMutex);
  return _hasTargetInfo ? _targetInfo.width : 0;
}

int RNSkWindowSurface::getHeight() {
  std::lock_guard<std::mutex> lock(_targetInfoMutex);
  return _hasTargetInfo ? _targetInfo.height : 0;
}

bool RNSkWindowSurface::getTargetInfo(RNSkGraphiteTargetInfo *info) {
  std::lock_guard<std::mutex> lock(_targetInfoMutex);
  if (!_hasTargetInfo) {
    return false;
  }
  *info = _targetInfo;
  return true;
}

void RNSkWindowSurface::updateTargetInfo() {
  std::lock_guard<std::mutex> lock(_targetInfoMutex);
  if (_nativeHandle == nullptr || !canvasOf(_nativeHandle)->hasRenderTarget()) {
    _hasTargetInfo = false;
    return;
  }
  _targetInfo.width = _width;
  _targetInfo.height = _height;
  _targetInfo.colorType = _colorType;
  _targetInfo.useP3ColorSpace = _useP3ColorSpace;
  _targetInfo.textureInfo = getTextureInfo();
  _hasTargetInfo = true;
}

skgpu::graphite::TextureInfo RNSkWindowSurface::getTextureInfo() const {
  return skgpu::graphite::TextureInfos::MakeDawn(
      skgpu::graphite::DawnTextureInfo(skgpu::graphite::SampleCount::k1,
                                       skgpu::Mipmapped::kNo, _format, _usage,
                                       wgpu::TextureAspect::All));
}

// The Graphite render target for the current size, rather than a swapchain.
// Named as upstream names it because it is called where upstream calls it,
// and it answers the same question: the surface a frame is replayed onto.
void RNSkWindowSurface::configureSurface() {
  if (_nativeHandle == nullptr) {
    return;
  }
  if (canvasOf(_nativeHandle)->renderTarget(_width, _height,
                                            _useP3ColorSpace) == nullptr) {
    RNSkLogger::logToConsole(
        "Could not make a %dx%d Graphite target for the canvas", _width,
        _height);
  }
}

bool RNSkWindowSurface::canPresent() {
  if (_nativeHandle == nullptr) {
    return false;
  }
  if (_canPresent && !_canPresent()) {
    // The frame is kept by the caller: ask for it again once the window can
    // show it.
    requestRedraw();
    return false;
  }
  return true;
}

namespace {

// Reads the surface back and hands the pixels to the canvas.
//
// This is what `_surface.Present()` is upstream. It does not wait: Graphite's
// readback is asynchronous and DawnContext keeps a thread polling for
// completions, so the main thread submits and returns. The frame has been
// consumed either way -- it simply appears a moment later, when the painter is
// asked to repaint.
//
// A weak reference to the canvas, because the readback outlives the frame: a
// view unmounted between submitting and completing would otherwise be written
// into after it was freed.
void readBackInto(basalt::BasaltSkiaCanvas *canvas, SkSurface *surface,
                  int width, int height, SkColorType colorType) {
  sk_sp<SkImage> image = surface->makeImageSnapshot();
  // The snapshot is a copy task on the surface's recorder: submitted here, so
  // that the read below has something complete to read.
  if (auto *recorder = surface->recorder()) {
    if (auto recording = recorder->snap()) {
      DawnContext::getInstance().submitRecording(recording.get());
    }
  }
  if (image == nullptr) {
    return;
  }
  const uint64_t sequence = canvas->beginFrame();
  std::weak_ptr<basalt::BasaltSkiaCanvas> weakCanvas =
      canvas->weak_from_this();
  DawnContext::getInstance().readPixels(
      image, SkIRect::MakeWH(width, height),
      [weakCanvas, sequence, width, height, colorType](ReadResult result) {
        if (auto strongCanvas = weakCanvas.lock()) {
          strongCanvas->deliverFrame(sequence, std::move(result), width, height,
                                     colorType);
        }
      });
}

} // namespace

bool RNSkWindowSurface::presentRecordings(
    const std::vector<skgpu::graphite::Recording *> &recordings) {
  if (!canPresent()) {
    return false;
  }
  auto *canvas = canvasOf(_nativeHandle);
  SkSurface *surface = canvas->renderTarget(_width, _height, _useP3ColorSpace);
  if (surface == nullptr) {
    return false;
  }
  const bool success =
      DawnContext::getInstance().insertRecordings(recordings, surface);
  readBackInto(canvas, surface, _width, _height, _colorType);
  return success;
}

bool RNSkWindowSurface::presentImage(const sk_sp<SkImage> &image) {
  if (!canPresent()) {
    return false;
  }
  auto *canvas = canvasOf(_nativeHandle);
  SkSurface *surface = canvas->renderTarget(_width, _height, _useP3ColorSpace);
  if (surface == nullptr) {
    return false;
  }
  surface->getCanvas()->drawImage(image, 0, 0);
  readBackInto(canvas, surface, _width, _height, _colorType);
  return true;
}

} // namespace RNSkia
