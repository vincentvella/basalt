// What Skia asks a platform for, answered by this one.
//
// `RNSkManager` -- the object that installs Skia's JavaScript bindings -- is
// built from three things: a JSI runtime, a call invoker, and one of these.
// React Native's Apple half supplies `RNSkApplePlatformContext`, which is
// nearly all usable here and is coupled to the one piece that is not: it takes
// an `RCTViewRegistry` for the screenshot service, and this platform has no
// RCTFabric. So the context is this project's, and it is short.
//
// ## Why it is short, which is version 3's doing
//
// Skia 2.x asks a platform for fifteen things: textures, native buffers,
// video, a GPU direct context, the lot. Version 3 moved to Graphite and asks
// for six, because Graphite does not hand that plumbing to the embedder.
// Those six are the whole of this file:
//
//   runOnMainThread          the platform's own UI thread seam
//   performStreamOperation   a URI read, which core/ImageBytes.h already does
//   makeOffscreenSurface     a surface to draw into with no window
//   createFontMgr            Skia's own CoreText font manager
//   raiseError               a log line and a crash report
//   takeScreenshotFromViewTag  a `<Canvas>` question, answered null until
//                              there is one
//
// Pure C++ rather than Objective-C++, which was not obvious and is worth
// saying: every Apple-specific answer either has a Skia API of its own
// (`SkFontMgr_New_CoreText`) or a seam this platform already has, so nothing
// here imports AppKit. That is what would let a Linux or Windows version of
// this file exist the day there is a Skia build for either.

#pragma once

#include "RNSkPlatformContext.h"

#include <ReactCommon/CallInvoker.h>

#include <exception>
#include <functional>
#include <memory>
#include <string>

namespace basalt {

class BasaltSkPlatformContext : public RNSkia::RNSkPlatformContext {
 public:
  explicit BasaltSkPlatformContext(
      std::shared_ptr<facebook::react::CallInvoker> callInvoker);

  // The UI thread, which is the one that owns the windows. `postToUiThread` is
  // the same seam every other part of this platform reaches the main thread
  // through, so Skia's main-thread work is ordered against everything else's
  // rather than racing it on a queue of its own.
  void runOnMainThread(std::function<void()> func) override;

  // A view's pixels, which needs a view. Nothing mounts a `<Canvas>` here yet,
  // so this answers null rather than inventing an image: `makeViewScreenshot`
  // in the base class passes that null straight to the caller's callback,
  // which is what an app sees. backlog/compatibility.md has the view.
  sk_sp<SkImage> takeScreenshotFromViewTag(size_t tag) override;

  // A `require()`d asset, a file, a data URI or an http one, read off the
  // thread that asked. `core/ImageBytes.h` already answers all four for
  // `<Image>`, including the bare path a release bundle leaves behind, so
  // Skia and `<Image>` resolve a source the same way by construction.
  void performStreamOperation(
      const std::string &sourceUri,
      const std::function<void(std::unique_ptr<SkStreamAsset>)> &op) override;

  // Logged and reported rather than thrown. Skia's own comment says this need
  // not stop execution and that callers must return afterwards, which is why
  // this does not throw: an exception here would unwind through Skia's C++
  // and out into whatever called it.
  void raiseError(const std::exception &err) override;

  // A surface with no window behind it, which is what an app gets from
  // `Skia.Surface.MakeOffscreen` and what every image filter draws into.
  // Raster, on the CPU: the GPU one is Graphite's and arrives with the window
  // context that a `<Canvas>` needs.
  sk_sp<SkSurface> makeOffscreenSurface(int width, int height,
                                        bool useP3ColorSpace = false) override;

  // Skia's own, which reads the system's fonts through Core Text -- the same
  // library this platform's AppKit text layer measures with, so a paragraph
  // and a Skia text run resolve a family name identically.
  sk_sp<SkFontMgr> createFontMgr() override;
};

} // namespace basalt
