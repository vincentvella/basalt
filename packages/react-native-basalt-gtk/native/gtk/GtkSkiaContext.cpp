#include "GtkSkiaContext.h"

#include "ImageBytes.h"
#include "PlatformServices.h"

#include <gtk/gtk.h>

#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "include/ports/SkFontMgr_fontconfig.h"
#include "include/ports/SkFontScanner_FreeType.h"

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

class GtkSkiaPlatformContext : public RNSkia::RNSkPlatformContext {
 public:
  GtkSkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker,
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
    // Logged, not fatal, as on the other two hosts: the package's Apple version
    // calls RCTFatal and takes the app down with a red box, and a shader that
    // fails to compile is not a reason to end a session.
    LOG(ERROR) << "react-native-skia: " << err.what();
  }

  // ----------------------------------------------------------------- surfaces

  sk_sp<SkSurface> makeOffscreenSurface(int width, int height,
                                        bool useP3ColorSpace = false) override {
    // The colour space is accepted and not honoured, as on Windows: a Display
    // P3 surface is a wide-gamut answer for a host that has no colour
    // management of its own yet, and silently returning sRGB is the same
    // picture on a machine that is not wide-gamut.
    (void)useP3ColorSpace;
    // CPU. See gtk/GtkSkiaContext.h for why this stage is raster even though
    // the archives carry a GL backend.
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
    // Fontconfig, through Skia's own port, which is what the Linux archives are
    // built with -- the GN args turn it on where the Windows build turns it
    // off. Same seam, each platform's real font system: CoreText on Apple,
    // DirectWrite on Windows, this here.
    //
    // Two arguments, which is newer than most examples of this call. m152
    // split the scanner out of the font manager -- `SkFontMgr_New_FontConfig`
    // used to take a config alone and now takes the thing that reads a font
    // file as well, because Skia has two of those to choose between: FreeType
    // and Fontations, its Rust one. FreeType, to match what the archives were
    // built with.
    //
    // A null FcConfig means "the current one", which is fontconfig's own
    // default -- the machine's /etc/fonts plus the user's. Passing one of our
    // own would mean owning font discovery, which is not this host's job.
    //
    // Made once and shared: this builds a fontconfig configuration each time
    // it is called, which is a scan of every font directory on the machine,
    // and createFontMgr is called per paragraph.
    static sk_sp<SkFontMgr> manager =
        SkFontMgr_New_FontConfig(nullptr, SkFontScanner_Make_FreeType());
    return manager;
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
    // on Android; neither exists here. GStreamer is the GTK answer and is its
    // own piece of work.
    throw std::runtime_error(
        "react-native-skia: video is not implemented on this host. It needs "
        "GStreamer, which nothing here wraps yet.");
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
        // checking, so a null here is a segfault rather than a missing image --
        // and a 404 on one asset would take the app down. Apple's
        // implementation reaches the same place by accident: a failed
        // `dataWithContentsOfURL:` leaves `data` nil, and nil's bytes and
        // length are null and zero, so it too hands over an empty stream.
        op(SkMemoryStream::Make(SkData::MakeEmpty()));
        return;
      }
      sk_sp<SkData> data = SkData::MakeWithCopy(bytes.data(), bytes.size());
      op(SkMemoryStream::Make(std::move(data)));
    }).detach();
  }

  // ------------------------------------------------- not implemented, and why

  sk_sp<SkImage> takeScreenshotFromViewTag(size_t tag) override {
    // Needs a tag-to-view lookup and a render of that widget into a surface.
    // The host has the first half -- the mounting manager keeps a tag registry
    // -- and nothing yet asks for the second. Named rather than blank: an empty
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
           "textures to share. See gtk/GtkSkiaContext.h.";
  }
};

} // namespace

std::shared_ptr<RNSkia::RNSkPlatformContext>
makeSkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker) {
  // The default display's scale, and one number for the whole session: Skia
  // asks for a density up front and there is no window yet when the module
  // installs. A window dragged to a monitor of another scale is a thing to fix
  // when something notices, which is the position all three hosts take.
  //
  // `gdk_display_get_monitors` needs a display, and there is one by the time a
  // TurboModule installs -- but not in a test binary that never opened one, so
  // a missing display is 1.0 rather than a crash.
  float density = 1.0f;
  if (GdkDisplay *display = gdk_display_get_default()) {
    if (GListModel *monitors = gdk_display_get_monitors(display)) {
      if (auto *monitor = static_cast<GdkMonitor *>(g_list_model_get_item(monitors, 0))) {
        const int scale = gdk_monitor_get_scale_factor(monitor);
        if (scale > 0) {
          density = static_cast<float>(scale);
        }
        g_object_unref(monitor);
      }
    }
  }
  return std::make_shared<GtkSkiaPlatformContext>(std::move(callInvoker), density);
}

} // namespace basalt
