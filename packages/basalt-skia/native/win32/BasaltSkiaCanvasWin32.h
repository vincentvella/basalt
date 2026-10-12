// The Win32 half of a `<Canvas>` on Skia 3.x: a Graphite surface with no
// window under it, and the Direct2D bitmap the paint walk draws.
//
// ## Why this host does not get a swapchain
//
// The other two hosts hand the package a surface of its own -- a CAMetalLayer
// on macOS, a GL surface on GTK -- and `RNSkWindowSurface` configures it as a
// Dawn swapchain and presents straight to the compositor. This host has
// neither a layer tree nor a view that is a window (the note at the top of
// `RnWin32View.h` says why: one HWND for the whole surface, and a view is a
// plain C++ object painted by a recursive Direct2D walk). There is no child
// surface to give Dawn and nothing to present to.
//
// So a canvas here renders into an offscreen Graphite target and lets the one
// Direct2D walk that draws everything else draw it too, at the rect the shadow
// tree already resolved. That is the same arrangement the 2.x peer beside this
// one made -- `Win32SkiaPeer.h` argues it at length and the argument has not
// changed -- with one difference that matters: 2.x drew on the CPU into a
// raster surface, and this draws on the GPU through Dawn and D3D12 and reads
// the result back. A readback is not free, but it replaces a software
// rasteriser, so the frame is cheaper than it was and not dearer.
//
// What the walk buys, and why this is not a worse arrangement than a layer:
// the canvas is clipped by `overflow: hidden`, moved by a ScrollView's offset,
// faded by `opacity` and turned by `transform` because the view above it did
// those things before calling down -- none of which a sibling layer gets for
// free. It also appears in the offscreen snapshot, because the snapshot is the
// same walk against a WIC target.
//
// ## The readback does not block the main thread
//
// Graphite reads pixels back asynchronously and `DawnContext` keeps a thread
// polling for completions, so the frame is handed over on that thread rather
// than waited for on this one. `presentRecordings` therefore returns as soon
// as the recordings are submitted: the frame *was* consumed, it simply arrives
// a moment later. Frames can then complete out of order, so each carries a
// sequence number and an older one that lands after a newer one is dropped.
//
// ## Where RNSkWindowSurface's state lives
//
// In this object, reached through the `void *nativeHandle` that `attach`
// takes. Not a design preference: upstream's `RNSkWindowSurface.h` is a header
// this package does not own, its members are a `wgpu::Surface` and the things
// around it, and there is nowhere in it to put an `sk_sp<SkSurface>`. basalt
// supplies the *bodies* of that class on this host (see
// `win32/RNSkWindowSurface.cpp`) and keeps the state that has no member here.
//
// One consequence worth naming: `RNSkWindowSurface::isAttached()` is inline in
// upstream's header and answers `_surface != nullptr` about the swapchain that
// never exists here, so it is always false on this host. Nothing in the
// package calls it -- only a platform peer does, and this is that peer, which
// asks `BasaltSkiaCanvas::hasRenderTarget()` instead.

#pragma once

#include "RnWin32View.h"

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/mounting/ShadowView.h>

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdocumentation"

#include "RNSkPlatformContext.h"

#include "include/core/SkColorType.h"
#include "include/core/SkImage.h"
#include "include/core/SkRefCnt.h"
#include "include/core/SkSurface.h"

#pragma clang diagnostic pop

#include <wrl/client.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

struct ID2D1Bitmap;
struct ID2D1RenderTarget;

namespace basalt {

// The pixel size a layout of this many points has at this density, which is
// what the surface is rendered at.
//
// Rounded rather than truncated, which is what upstream's own SkiaView.mm does
// and for the reason it gives: a product like 1169.9999 should give the pixel
// size the layout means. Zero or negative for a layout that has none -- Yoga
// hands those out before the first real layout -- and the caller renders
// nothing then.
//
// The same function as the AppKit canvas's, and deliberately so: the two hosts
// present differently and round identically, so a `<Canvas>` is the same
// number of pixels on both.
std::pair<int, int> skiaCanvasPixelSize(double widthInPoints,
                                        double heightInPoints, double density);

// Everything one `<Canvas>` owns on this host: the Graphite surface frames are
// replayed onto, the last frame read back off the GPU, and the Direct2D bitmap
// the paint walk draws.
//
// Two roles on one object for the reason the 2.x peer gives: they are two
// views of one frame, and an object between them would only mean keeping two
// pointers to the same pixels in step. The surface side is driven by
// `RNSkWindowSurface`; the painter side by the Direct2D walk.
//
// Threading. The surface side is the main thread (attach, resize and present
// all arrive there). `deliverFrame` is whichever thread finished the readback.
// `draw` is the main thread, from WM_PAINT. The frame itself is handed between
// them under `mutex_`, and nothing else crosses.
class BasaltSkiaCanvas final
    : public win32::RnWin32Painter,
      public std::enable_shared_from_this<BasaltSkiaCanvas> {
public:
  BasaltSkiaCanvas(std::shared_ptr<RNSkia::RNSkPlatformContext> context,
                   std::function<void()> requestRepaint);
  ~BasaltSkiaCanvas() override;

  BasaltSkiaCanvas(const BasaltSkiaCanvas &) = delete;
  BasaltSkiaCanvas &operator=(const BasaltSkiaCanvas &) = delete;

  // Replaced on every mutation rather than kept from the first one. A canvas
  // that mounted before the host installed its repaint callback would
  // otherwise hold an empty function for as long as it lives, render every
  // frame perfectly and show none of them. (The 2.x peer learned this.)
  void setRequestRepaint(std::function<void()> requestRepaint);

  // --- The surface side, called by RNSkWindowSurface ------------------------

  // The Graphite surface frames are replayed onto, at this size in pixels and
  // this color space: made on first use, remade when either changes, and
  // nullptr when Graphite would not make one. Main thread.
  SkSurface *renderTarget(int width, int height, bool useP3ColorSpace);

  // Whether there is one. What `RNSkWindowSurface::isAttached()` would answer
  // if it could; see the note at the top of this file.
  bool hasRenderTarget() const { return surface_ != nullptr; }

  // The size of the current render target in pixels, or nothing without one.
  std::optional<std::pair<int, int>> renderTargetSize() const;

  // Drops it, on detach. Main thread.
  void releaseRenderTarget();

  // The sequence number of the frame about to be recorded. Main thread, in
  // the order frames are submitted.
  uint64_t beginFrame() { return ++submittedSequence_; }

  // A frame's pixels, read back off the GPU. `result` may be null (the read
  // failed), and a `sequence` older than one already delivered is dropped --
  // readbacks complete in whatever order the GPU finishes them. Called on the
  // readback thread, inside DawnContext's lock, so this does as little as it
  // can and posts the repaint to the main thread.
  void deliverFrame(uint64_t sequence,
                    std::unique_ptr<const SkImage::AsyncReadResult> result,
                    int width, int height, SkColorType colorType);

  // How many frames have been delivered. For tests: a frame arrives on the
  // readback thread, so there is nothing on the main thread to wait on.
  uint64_t deliveredFrames() const;

  // --- RnWin32Painter -------------------------------------------------------

  void draw(ID2D1RenderTarget *target, float boxWidth,
            float boxHeight) override;

private:
  // The Direct2D copy of the last frame: made once per target, refilled when a
  // newer frame has arrived. Keyed on the target the way RnWin32Image's cache
  // is, and for the same reason -- a bitmap belongs to the target that made
  // it, and this host draws into a second one for the offscreen snapshot.
  bool ensureDeviceBitmap(ID2D1RenderTarget *target, int width, int height,
                          SkColorType colorType);
  void releaseDeviceBitmap();

  std::shared_ptr<RNSkia::RNSkPlatformContext> context_;
  std::function<void()> requestRepaint_;

  // Main thread only.
  sk_sp<SkSurface> surface_;
  int surfaceWidth_ = 0;
  int surfaceHeight_ = 0;
  bool surfaceUsesP3_ = false;
  uint64_t submittedSequence_ = 0;

  // The frame, handed from the readback thread to the paint walk. A shared_ptr
  // rather than the unique_ptr the readback gives, so `draw` can take a
  // reference under the lock and read the pixels without holding it -- the
  // readback thread is inside DawnContext's lock when it delivers, and every
  // other view's frames are waiting on that lock.
  mutable std::mutex mutex_;
  std::shared_ptr<const SkImage::AsyncReadResult> frame_;
  uint64_t deliveredSequence_ = 0;
  int frameWidth_ = 0;
  int frameHeight_ = 0;
  SkColorType frameColorType_ = kUnknown_SkColorType;
  // Bumped by every delivered frame. The device bitmap remembers which
  // generation it holds, so a window that repaints for some other reason -- a
  // resize, a sibling changing -- redraws the canvas without copying its
  // pixels again.
  uint64_t generation_ = 0;

  // Main thread only. Which generation the bitmap below already holds.
  uint64_t deviceGeneration_ = 0;
  Microsoft::WRL::ComPtr<ID2D1Bitmap> deviceBitmap_;
  ID2D1RenderTarget *deviceTarget_ = nullptr;
  int deviceWidth_ = 0;
  int deviceHeight_ = 0;
};

// `applySkiaCanvas` and `forgetSkiaCanvas` -- what the mounting manager calls
// -- are declared in `win32/Win32SkiaPeer.h` beside the 2.x pair rather than
// here, for the reason AppKitSkiaPeer.h gives: the manager calls whichever of
// the two generations was linked and must not know which. This header is for
// the canvas itself and for the tests.

// The size in pixels the canvas for `tag` renders at, or nothing when there is
// no canvas or it has no surface yet. For tests: a Graphite surface's size is
// not readable off anything the host owns.
std::optional<std::pair<int, int>>
skiaCanvasAttachedSize(facebook::react::Tag tag);

// How many frames the canvas for `tag` has had delivered, and zero when there
// is no canvas. For tests, for the reason above: a frame is handed over by
// whichever thread finished reading it back.
uint64_t skiaCanvasFrameCount(facebook::react::Tag tag);

} // namespace basalt
