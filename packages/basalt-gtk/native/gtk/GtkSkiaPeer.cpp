#include "GtkSkiaPeer.h"

#include "GtkSkiaModule.h"
#include "RnView.h"
#include "SkiaPictureViewComponent.h"

#include "PlatformServices.h"

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
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace basalt {
namespace {

// The surface a `<Canvas>` renders into, and the thing that hands it over.
//
// Only one interface here, where the Win32 peer needs two: this host's view
// already draws a texture, so presenting is a call rather than a drawing path
// of its own.
class GtkSkiaCanvas final : public RNSkia::RNSkCanvasProvider {
 public:
  GtkSkiaCanvas(std::function<void()> requestRedraw,
                std::shared_ptr<RNSkia::RNSkPlatformContext> context,
                RnView *view)
      : RNSkia::RNSkCanvasProvider(std::move(requestRedraw)),
        context_(std::move(context)) {
    // Weakly, because the widget belongs to the mounting registry and is
    // destroyed on a Delete mutation -- which can happen while a picture from
    // the frame before is still on its way to the main thread. A strong
    // reference here would keep a disposed widget alive to be drawn into;
    // a raw pointer would be a use-after-free on the same path.
    g_weak_ref_init(&view_, view);
  }

  // No `override`: RNSkCanvasProvider has no virtual destructor. This still
  // runs, because the shared_ptr is built by make_shared<GtkSkiaCanvas> and
  // records the concrete deleter then -- not because anything upcasts safely.
  ~GtkSkiaCanvas() { g_weak_ref_clear(&view_); }

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
      // GdkTexture is a GObject and a widget may only be touched from the
      // thread that owns the main context, so the handover below cannot happen
      // here. Ask for a redraw and let it arrive on the right thread -- the
      // same answer the package's Metal provider gives off the main thread.
      _requestRedraw();
      return false;
    }
    if (surface_ == nullptr) {
      return false;
    }
    callback(surface_->getCanvas());

    RnView *view = static_cast<RnView *>(g_weak_ref_get(&view_));
    if (view == nullptr) {
      // The widget went away between the picture and this. Not an error: a
      // canvas unmounted mid-frame does exactly that.
      return true;
    }

    SkPixmap pixels;
    if (!surface_->peekPixels(&pixels)) {
      // Only a GPU-backed surface cannot be peeked, and this one is raster by
      // construction. A line rather than a silent blank frame, for the day
      // that changes.
      LOG(WARNING) << "react-native-skia: a <Canvas> surface could not be read back";
      g_object_unref(view);
      return false;
    }

    // Copied, not referenced. GdkMemoryTexture takes the bytes as its own and
    // requires them not to change afterwards, and these are about to: the next
    // frame draws into this very surface. The copy is what makes the texture a
    // snapshot rather than a window onto a buffer being overwritten.
    GBytes *bytes = g_bytes_new(pixels.addr(), pixels.computeByteSize());
    // N32Premul is premultiplied BGRA on a little-endian machine, which is
    // what this format names. Skia and GDK agree here, so the copy above is a
    // memcpy rather than a conversion -- worth stating, because if that ever
    // stops being true the picture goes blue and nothing says why.
    GdkTexture *texture = gdk_memory_texture_new(pixelWidth_,
                                                 pixelHeight_,
                                                 GDK_MEMORY_B8G8R8A8_PREMULTIPLIED,
                                                 bytes,
                                                 pixels.rowBytes());
    g_bytes_unref(bytes);

    // STRETCH, because the surface was made at exactly this view's size: there
    // is nothing to fit. Any other mode would be a no-op that costs a
    // comparison, and would quietly letterbox if the two ever disagreed.
    rn_view_set_texture(view, texture, RN_IMAGE_FIT_STRETCH);
    g_object_unref(texture);
    g_object_unref(view);
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
    if (pixelWidth_ > 0 && pixelHeight_ > 0) {
      surface_ = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(pixelWidth_, pixelHeight_));
      if (surface_ == nullptr) {
        LOG(WARNING) << "react-native-skia: could not make a " << pixelWidth_ << "x"
                     << pixelHeight_ << " surface for a <Canvas>";
      } else {
        // A raster surface is not promised zeroed pixels, and there is a
        // window between this and the first picture in which the canvas is
        // drawn. Uninitialised memory is a striking thing to put on screen.
        surface_->getCanvas()->clear(SK_ColorTRANSPARENT);
      }
    }
    // The picture drawn at the old size has to be drawn again at this one, and
    // nothing else would ask.
    _requestRedraw();
  }

 private:
  std::shared_ptr<RNSkia::RNSkPlatformContext> context_;
  GWeakRef view_;

  sk_sp<SkSurface> surface_;
  int pixelWidth_ = 0;
  int pixelHeight_ = 0;
};

// The package's view, holding the canvas above.
//
// `RNSkAppleView.h` is this in template form and cannot be included here: its
// first import is the Apple platform context, which is Objective-C++ over an
// RCTBridge. A `<Canvas>` needs exactly one RNSkView subclass, so this is that
// one rather than a template over the one.
class GtkSkiaPictureView final : public RNSkia::RNSkPictureView {
 public:
  GtkSkiaPictureView(const std::shared_ptr<RNSkia::RNSkPlatformContext> &context, RnView *view)
      : RNSkia::RNSkPictureView(
            context,
            std::make_shared<GtkSkiaCanvas>(
                // Bound before the base class is constructed, exactly as the
                // package's own views bind it: nothing calls it until a
                // picture arrives, by which time this object is whole.
                std::bind(&RNSkia::RNSkView::requestRedraw, this),
                context,
                view)) {}

  // `getCanvasProvider` is protected on RNSkView, which is why this is a
  // member rather than a cast at the call site.
  std::shared_ptr<GtkSkiaCanvas> canvas() {
    return std::static_pointer_cast<GtkSkiaCanvas>(getCanvasProvider());
  }
};

struct Attached {
  std::shared_ptr<GtkSkiaPictureView> view;
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
bool parseNativeId(const char *text, size_t &out) {
  if (text == nullptr || *text == '\0') {
    return false;
  }
  const std::string value(text);
  try {
    size_t consumed = 0;
    const unsigned long long parsed = std::stoull(value, &consumed);
    if (consumed != value.size()) {
      return false;
    }
    out = static_cast<size_t>(parsed);
    return true;
  } catch (const std::exception &) {
    return false;
  }
}

} // namespace

bool applySkiaCanvas(GtkWidget *view, const facebook::react::ShadowView &shadowView) {
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
  if (!parseNativeId(rn_view_get_native_id(RN_VIEW(view)), nativeId)) {
    // Not an error: a SkiaPictureView can mount before its nativeID has been
    // applied, and the next updateView carries it.
    return true;
  }

  RNSkia::RNSkManager *manager = GtkSkiaModule::manager();
  if (manager == nullptr) {
    LOG(WARNING) << "SkiaPictureView tag " << tag
                 << " mounted before RNSkiaModule.install ran, so it has no canvas";
    return true;
  }

  auto &entry = attached()[tag];
  if (entry.view == nullptr) {
    entry.view = std::make_shared<GtkSkiaPictureView>(manager->getPlatformContext(),
                                                      RN_VIEW(view));
    entry.nativeId = nativeId;
    manager->setSkiaView(nativeId, entry.view);
    LOG(INFO) << "SkiaPictureView tag " << tag << " registered as Skia view " << nativeId;
  } else if (entry.nativeId != nativeId) {
    // The id moved. Re-register rather than leave the manager pointing the old
    // surface at the new id's pictures.
    manager->unregisterSkiaView(entry.nativeId);
    entry.nativeId = nativeId;
    manager->setSkiaView(nativeId, entry.view);
  }

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
  if (RNSkia::RNSkManager *manager = GtkSkiaModule::manager()) {
    manager->unregisterSkiaView(it->second.nativeId);
  }
  attached().erase(it);
}

} // namespace basalt
