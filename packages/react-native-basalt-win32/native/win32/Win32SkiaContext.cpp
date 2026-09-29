#include "Win32SkiaContext.h"

#include "ImageBytes.h"
#include "PlatformServices.h"

#include <windows.h>

#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkTypeface_win.h"

#include <glog/logging.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace basalt {
namespace {

class Win32SkiaPlatformContext : public RNSkia::RNSkPlatformContext {
 public:
  Win32SkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker,
                           float pixelDensity)
      : RNSkia::RNSkPlatformContext(std::move(callInvoker), pixelDensity) {}

  // ---------------------------------------------------------------- threading

  void runOnMainThread(std::function<void()> func) override {
    // basalt's own, so Skia's idea of the main thread and this host's are the
    // same one. Fire and forget, which is all `postToUiThread` offers and all
    // this seam promises.
    postToUiThread(std::move(func));
  }

  // ------------------------------------------------------------------- errors

  void raiseError(const std::exception &err) override {
    // Logged, not fatal, as on AppKit: the package's Apple version calls
    // RCTFatal and takes the app down with a red box, and a shader that fails
    // to compile is not a reason to end a session.
    LOG(ERROR) << "react-native-skia: " << err.what();
  }

  // ----------------------------------------------------------------- surfaces

  sk_sp<SkSurface> makeOffscreenSurface(int width, int height,
                                        bool useP3ColorSpace = false) override {
    // The colour space is accepted and not honoured: a Display P3 surface is a
    // wide-gamut answer for a host that has no colour management of its own
    // yet, and silently returning sRGB is the same picture on a machine that
    // is not wide-gamut. Named here so the day it matters, this is the line.
    (void)useP3ColorSpace;
    // CPU. See Win32SkiaContext.h for why this host is raster: everything the
    // imperative API does -- paths, decoded images, typefaces -- works here,
    // and so does `<Canvas>`, which draws into one of these and is then copied
    // into the Direct2D walk by win32/Win32SkiaPeer.h.
    return SkSurfaces::Raster(SkImageInfo::MakeN32Premul(width, height));
  }

  GrDirectContext *getDirectContext() override {
    // Null is a documented answer rather than a failure: Skia's own call sites
    // either skip the GPU path for a raster image or say "No GPU context
    // available" by name. What must not happen is inventing a context that
    // draws nowhere.
    return nullptr;
  }

  // ------------------------------------------------------------------- images

  sk_sp<SkImage> makeImageFromNativeBuffer(void *) override {
    throw std::runtime_error(unsupportedNativeBuffers());
  }

  sk_sp<SkImage> makeImageFromNativeTexture(const RNSkia::TextureInfo &, int, int,
                                            bool) override {
    throw std::runtime_error(unsupportedTextures());
  }

  const RNSkia::TextureInfo getTexture(sk_sp<SkImage>) override {
    throw std::runtime_error(unsupportedTextures());
  }

  const RNSkia::TextureInfo getTexture(sk_sp<SkSurface>) override {
    throw std::runtime_error(unsupportedTextures());
  }

  // -------------------------------------------------------------------- fonts

  sk_sp<SkFontMgr> createFontMgr() override {
    // DirectWrite, which is what the Windows archives are built with -- the GN
    // args turn freetype and fontconfig off, where the Linux build turns them
    // on. Same seam, each platform's real font system.
    return SkFontMgr_New_DirectWrite();
  }

  std::vector<std::string> getSystemFontFamilies() override {
    std::vector<std::string> families;
    const sk_sp<SkFontMgr> manager = createFontMgr();
    if (manager == nullptr) {
      return families;
    }
    const int count = manager->countFamilies();
    families.reserve(static_cast<size_t>(count));
    for (int i = 0; i < count; i++) {
      SkString name;
      manager->getFamilyName(i, &name);
      families.emplace_back(name.c_str());
    }
    return families;
  }

  // -------------------------------------------------------------------- media

  std::shared_ptr<RNSkia::RNSkVideo> createVideo(const std::string &) override {
    // The package's video is AVFoundation on Apple and MediaCodec through JNI
    // on Android; neither exists here. Media Foundation is the Windows answer
    // and is its own piece of work -- the same conclusion kino's own audio
    // module reached for the same reason.
    throw std::runtime_error(
        "react-native-skia: video is not implemented on this host. It needs Media "
        "Foundation, which nothing here wraps yet.");
  }

  // ------------------------------------------------------------------ loading

  void performStreamOperation(
      const std::string &sourceUri,
      const std::function<void(std::unique_ptr<SkStreamAsset>)> &op) override {
    // On a thread, because callers treat this as asynchronous and the operation
    // may read from the network. Detached for the same reason the package's is:
    // the continuation is the completion.
    //
    // Through core's own fetcher rather than a second one written here, so a
    // URI means to Skia what it means to <Image>: file, data and http, resolved
    // the same way in both.
    std::thread([sourceUri, op]() {
      std::string bytes;
      std::string error;
      if (!fetchImageBytes(sourceUri, &bytes, &error)) {
        LOG(WARNING) << "react-native-skia: could not read " << sourceUri << ": " << error;
        // An empty stream, not a null one. The continuation in
        // JsiSkDataFactory::fromURI calls `stream->getLength()` without
        // checking, so a null here is an access violation rather than a
        // missing image -- and a 404 on one asset would take the app down.
        // Apple's implementation reaches the same place by accident: a failed
        // `dataWithContentsOfURL:` leaves `data` nil, and nil's bytes and
        // length are null and zero, so it too hands over an empty stream.
        // Doing it deliberately keeps the two hosts on the same behaviour --
        // the promise resolves with no bytes, the image draws as nothing, and
        // the reason is in the log above.
        op(SkMemoryStream::Make(SkData::MakeEmpty()));
        return;
      }
      sk_sp<SkData> data = SkData::MakeWithCopy(bytes.data(), bytes.size());
      op(SkMemoryStream::Make(std::move(data)));
    }).detach();
  }

  // ------------------------------------------------- not implemented, and why

  sk_sp<SkImage> takeScreenshotFromViewTag(size_t tag) override {
    // Needs a tag-to-view lookup and a render of that view into a surface. The
    // host has the first half -- the mounting manager keeps a tag registry --
    // and nothing yet asks for the second. Named rather than blank: an empty
    // image here would read as a broken renderer.
    throw std::runtime_error(
        "react-native-skia: makeImageSnapshot of a view is not implemented on this "
        "host (tag " +
        std::to_string(tag) + "). Render to an offscreen surface instead.");
  }

  uint64_t makeNativeBuffer(sk_sp<SkImage>) override {
    throw std::runtime_error(unsupportedNativeBuffers());
  }

  uint64_t makeTestNativeBuffer(int, int) override {
    throw std::runtime_error(unsupportedNativeBuffers());
  }

  void releaseNativeBuffer(uint64_t) override {
    // Deliberately quiet: nothing here hands out a buffer, so nothing can
    // return one, and throwing from a release path would turn a leak into a
    // crash.
  }

 private:
  static std::string unsupportedNativeBuffers() {
    return "react-native-skia: native buffer interchange is not implemented on this "
           "host. It is used for camera and video frame sharing, which nothing here "
           "provides yet.";
  }

  static std::string unsupportedTextures() {
    return "react-native-skia: this host has no GPU context yet, so there are no "
           "textures to share. See win32/Win32SkiaContext.h.";
  }
};

} // namespace

std::shared_ptr<RNSkia::RNSkPlatformContext>
makeSkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker) {
  // The system DPI rather than a window's: Skia asks for one density up front,
  // and there is no window yet when the module installs. A window dragged to a
  // display of another scale is a thing to fix when something notices, which is
  // the same position the AppKit half takes with NSScreen.mainScreen.
  const UINT dpi = GetDpiForSystem();
  const float density = dpi > 0 ? static_cast<float>(dpi) / 96.0f : 1.0f;
  return std::make_shared<Win32SkiaPlatformContext>(std::move(callInvoker), density);
}

} // namespace basalt
