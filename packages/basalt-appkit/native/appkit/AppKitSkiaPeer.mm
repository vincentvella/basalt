#include "AppKitSkiaPeer.h"

#include "AppKitSkiaModule.h"
#include "AppKitSkiaView.h"
#include "SkiaPictureViewComponent.h"

#import "RnAppKitView.h"

#include "RNSkPictureView.h"

#include <glog/logging.h>

#include <memory>
#include <string>
#include <unordered_map>

namespace basalt {
namespace {

using Canvas = SkiaDrawingViewOf<RNSkia::RNSkPictureView>;

struct Attached {
  std::shared_ptr<Canvas> canvas;
  // The id it was registered under, kept so the unregister matches: a view whose
  // nativeID changed would otherwise leave the old registration behind, pointing
  // at a canvas nothing draws into.
  size_t nativeId = 0;
};

std::unordered_map<facebook::react::Tag, Attached> &attached() {
  static std::unordered_map<facebook::react::Tag, Attached> map;
  return map;
}

// `<Canvas>` stringifies a counter into nativeID. Anything else on a
// SkiaPictureView is not ours to interpret.
bool parseNativeId(NSString *identifier, size_t &out) {
  if (identifier == nil || identifier.length == 0) {
    return false;
  }
  const std::string text = identifier.UTF8String;
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

bool applySkiaCanvas(RnAppKitView *view, const facebook::react::ShadowView &shadowView) {
  if (shadowView.componentName == nullptr ||
      std::string(shadowView.componentName) !=
          facebook::react::SkiaPictureViewComponentName) {
    return false;
  }

  const auto tag = shadowView.tag;
  // In points, which is what the drawing view wants: the canvas provider scales
  // by the layer's contentsScale itself, and passing pixels here draws at twice
  // the size on a Retina display.
  const auto &frame = shadowView.layoutMetrics.frame;
  const int width = static_cast<int>(frame.size.width);
  const int height = static_cast<int>(frame.size.height);

  size_t nativeId = 0;
  if (!parseNativeId(view.rnNativeId, nativeId)) {
    // Not an error: a SkiaPictureView is mounted before its nativeID has been
    // applied in some orders, and the next updateView carries it.
    return true;
  }

  RNSkia::RNSkManager *manager = AppKitSkiaModule::manager();
  if (manager == nullptr) {
    LOG(WARNING) << "SkiaPictureView tag " << tag
                 << " mounted before RNSkiaModule.install ran, so it has no canvas";
    return true;
  }

  auto &entry = attached()[tag];
  if (entry.canvas == nullptr) {
    entry.canvas = std::make_shared<Canvas>(manager->getPlatformContext());
    entry.nativeId = nativeId;
    // Under the view's own layer, and below nothing: a canvas has no React
    // children to sit above, and the package adds it the same way.
    [view.layer addSublayer:entry.canvas->layer()];
    manager->setSkiaView(nativeId, entry.canvas->drawView());
    LOG(INFO) << "SkiaPictureView tag " << tag << " registered as Skia view " << nativeId;
  } else if (entry.nativeId != nativeId) {
    // The id moved. Re-register rather than leave the manager pointing the old
    // canvas at the new id's pictures.
    manager->unregisterSkiaView(entry.nativeId);
    entry.nativeId = nativeId;
    manager->setSkiaView(nativeId, entry.canvas->drawView());
  }

  if (width > 0 && height > 0) {
    entry.canvas->setSize(width, height);
  }
  return true;
}

void forgetSkiaCanvas(facebook::react::Tag tag) {
  auto it = attached().find(tag);
  if (it == attached().end()) {
    return;
  }
  if (RNSkia::RNSkManager *manager = AppKitSkiaModule::manager()) {
    manager->unregisterSkiaView(it->second.nativeId);
  }
  if (it->second.canvas != nullptr) {
    [it->second.canvas->layer() removeFromSuperlayer];
  }
  attached().erase(it);
}

} // namespace basalt
