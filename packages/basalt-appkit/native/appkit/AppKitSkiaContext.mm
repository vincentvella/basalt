#include "AppKitSkiaContext.h"

#include "PlatformServices.h"

#include "MetalContext.h"
#include "RNSkAppleVideo.h"

#import <AppKit/AppKit.h>
#import <Metal/Metal.h>

#include "include/core/SkCanvas.h"
#include "include/core/SkData.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkStream.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/SkImageGanesh.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
#include "include/ports/SkFontMgr_mac_ct.h"

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

SkColorType colorTypeFor(MTLPixelFormat format) {
  // The package's own table, which is the one Skia's Metal backend agrees with.
  switch (format) {
  case MTLPixelFormatRGBA8Unorm:
    return kRGBA_8888_SkColorType;
  case MTLPixelFormatBGRA8Unorm:
    return kBGRA_8888_SkColorType;
  case MTLPixelFormatRGB10A2Unorm:
    return kRGBA_1010102_SkColorType;
  case MTLPixelFormatR8Unorm:
    return kGray_8_SkColorType;
  case MTLPixelFormatRGBA16Float:
    return kRGBA_F16_SkColorType;
  case MTLPixelFormatRG8Unorm:
    return kR8G8_unorm_SkColorType;
  case MTLPixelFormatR16Float:
    return kA16_float_SkColorType;
  case MTLPixelFormatRG16Float:
    return kR16G16_float_SkColorType;
  case MTLPixelFormatR16Unorm:
    return kA16_unorm_SkColorType;
  case MTLPixelFormatRG16Unorm:
    return kR16G16_unorm_SkColorType;
  case MTLPixelFormatRGBA16Unorm:
    return kR16G16B16A16_unorm_SkColorType;
  case MTLPixelFormatRGBA8Unorm_sRGB:
    return kSRGBA_8888_SkColorType;
  default:
    return kUnknown_SkColorType;
  }
}

class AppKitSkiaPlatformContext : public RNSkia::RNSkPlatformContext {
public:
  AppKitSkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker,
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
    // Logged, not fatal. The package's Apple version calls `RCTFatal`, which
    // takes the app down with a red box; there is no red box here yet and a
    // shader that fails to compile is not a reason to end a session. When there
    // is one, this is the single line that should reach it.
    LOG(ERROR) << "react-native-skia: " << err.what();
  }

  // ------------------------------------------------------------------ surfaces

  sk_sp<SkSurface> makeOffscreenSurface(int width, int height,
                                        bool useP3ColorSpace = false) override {
    // The one that matters most for a first port: an app rendering frames for
    // export goes through here, not through a view.
    return MetalContext::getInstance().MakeOffscreen(width, height, useP3ColorSpace);
  }

  GrDirectContext *getDirectContext() override {
    return MetalContext::getInstance().getDirectContext();
  }

  // ------------------------------------------------------------------- images

  sk_sp<SkImage> makeImageFromNativeBuffer(void *buffer) override {
    return MetalContext::getInstance().MakeImageFromBuffer(buffer);
  }

  sk_sp<SkImage> makeImageFromNativeTexture(const RNSkia::TextureInfo &info, int width,
                                            int height, bool mipMapped) override {
    id<MTLTexture> texture = (__bridge id<MTLTexture>)(info.mtlTexture);
    const SkColorType colorType = colorTypeFor(texture.pixelFormat);
    if (colorType == kUnknown_SkColorType) {
      throw std::runtime_error("react-native-skia: unsupported Metal pixel format");
    }
    GrMtlTextureInfo mtlInfo;
    mtlInfo.fTexture.retain((__bridge const void *)texture);
    GrBackendTexture backend = GrBackendTextures::MakeMtl(
        width, height, mipMapped ? skgpu::Mipmapped::kYes : skgpu::Mipmapped::kNo, mtlInfo);
    return SkImages::BorrowTextureFrom(getDirectContext(), backend, kTopLeft_GrSurfaceOrigin,
                                       colorType, kPremul_SkAlphaType, nullptr);
  }

  const RNSkia::TextureInfo getTexture(sk_sp<SkImage> image) override {
    GrBackendTexture texture;
    if (!SkImages::GetBackendTextureFromImage(image, &texture, true)) {
      throw std::runtime_error("react-native-skia: no backend texture for this image");
    }
    return metalTextureInfo(texture);
  }

  const RNSkia::TextureInfo getTexture(sk_sp<SkSurface> surface) override {
    GrBackendTexture texture = SkSurfaces::GetBackendTexture(
        surface.get(), SkSurfaces::BackendHandleAccess::kFlushRead);
    return metalTextureInfo(texture);
  }

  // -------------------------------------------------------------------- fonts

  sk_sp<SkFontMgr> createFontMgr() override { return SkFontMgr_New_CoreText(nullptr); }

  std::vector<std::string> getSystemFontFamilies() override {
    std::vector<std::string> families;
    NSArray<NSString *> *names = NSFontManager.sharedFontManager.availableFontFamilies;
    families.reserve(names.count);
    for (NSString *name in names) {
      families.emplace_back(name.UTF8String);
    }
    return families;
  }

  // -------------------------------------------------------------------- media

  std::shared_ptr<RNSkia::RNSkVideo> createVideo(const std::string &url) override {
    // The package's own AVFoundation implementation, which is one of the files
    // that mentions no React and so compiles here unchanged.
    return std::make_shared<RNSkia::RNSkAppleVideo>(url, this);
  }

  // ------------------------------------------------------------------ loading

  void performStreamOperation(
      const std::string &sourceUri,
      const std::function<void(std::unique_ptr<SkStreamAsset>)> &op) override {
    // On a thread, because callers treat this as asynchronous and the operation
    // may read from the network. Detached for the same reason the package's is:
    // the continuation is the completion.
    //
    // No asset-catalog branch. The package falls back to `[NSImage imageNamed:]`
    // for a bare name with no scheme and no extension, which is an iOS app
    // bundle's image set; a desktop host has files and URLs, and a bare name
    // that reaches here is a bug worth seeing rather than silently resolving.
    std::thread([sourceUri, op]() {
      @autoreleasepool {
        NSString *uri = [NSString stringWithUTF8String:sourceUri.c_str()];
        NSURL *url = [NSURL URLWithString:uri];
        NSData *data = nil;
        if (url != nil && url.scheme != nil) {
          data = [NSData dataWithContentsOfURL:url];
        } else {
          // A path rather than a URL, which is what a bundled asset looks like
          // when the bundle was built on disk.
          data = [NSData dataWithContentsOfFile:uri];
        }
        if (data == nil) {
          LOG(WARNING) << "react-native-skia: could not read " << sourceUri;
          op(nullptr);
          return;
        }
        sk_sp<SkData> copy = SkData::MakeWithCopy(data.bytes, data.length);
        op(SkMemoryStream::Make(copy));
      }
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
        "host (tag " + std::to_string(tag) + "). Render to an offscreen surface instead.");
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
  static const RNSkia::TextureInfo metalTextureInfo(const GrBackendTexture &texture) {
    if (!texture.isValid()) {
      throw std::runtime_error("react-native-skia: invalid backend texture");
    }
    GrMtlTextureInfo mtlInfo;
    if (!GrBackendTextures::GetMtlTextureInfo(texture, &mtlInfo)) {
      throw std::runtime_error("react-native-skia: no Metal texture info");
    }
    RNSkia::TextureInfo info;
    info.mtlTexture = mtlInfo.fTexture.get();
    return info;
  }

  static std::string unsupportedNativeBuffers() {
    return "react-native-skia: CVPixelBuffer interchange is not implemented on this "
           "host. It is used for camera and video frame sharing, which nothing here "
           "provides yet.";
  }
};

} // namespace

std::shared_ptr<RNSkia::RNSkPlatformContext>
makeSkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker) {
  const float density =
      NSScreen.mainScreen != nil ? static_cast<float>(NSScreen.mainScreen.backingScaleFactor) : 1.0f;
  return std::make_shared<AppKitSkiaPlatformContext>(std::move(callInvoker), density);
}

} // namespace basalt
