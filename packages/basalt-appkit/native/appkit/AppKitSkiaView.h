// The drawing half of a `<Canvas>`, which is Skia's and not ours.
//
// `RNSkAppleView.h` in the package is this, in fifty header-only lines, and
// basalt cannot include it: its first `#import` is
// "RNSkApplePlatformContext.h", whose own first line is
// <React/RCTBridge+Private.h>. Nothing in the template needs either -- it uses
// `RNSkia::RNSkPlatformContext`, which is portable C++ in cpp/rnskia, and
// `RNSkMetalCanvasProvider`, which compiles here already. So this is the same
// template with the include it does not need left out.
//
// Copied rather than shimmed. A shim `React/RCTBridge+Private.h` would make the
// original compile and would then have to keep making it compile, for a header
// whose contents are not ours to predict. Fifty lines that say what they do are
// the cheaper of the two, and the thing being copied is a bridge between two
// interfaces rather than logic that can drift.

#pragma once

#import <QuartzCore/QuartzCore.h>

#include "RNSkMetalCanvasProvider.h"
#include "RNSkPlatformContext.h"
#include "RNSkView.h"

#include <functional>
#include <memory>

namespace basalt {

// What the AppKit view needs of whatever is drawing into it, without knowing
// which RNSkView subclass that is.
class SkiaDrawingView {
public:
  virtual ~SkiaDrawingView() = default;
  virtual CALayer *layer() = 0;
  virtual void setSize(int width, int height) = 0;
  virtual std::shared_ptr<RNSkia::RNSkView> drawView() = 0;
};

// T is an RNSkView subclass -- RNSkPictureView for a `<Canvas>`. The canvas
// provider is Metal's, and its redraw callback is the view's own
// `requestRedraw`, which is how a picture pushed from JavaScript reaches the
// screen without anybody polling.
template <class T> class SkiaDrawingViewOf final : public SkiaDrawingView, public T {
public:
  explicit SkiaDrawingViewOf(std::shared_ptr<RNSkia::RNSkPlatformContext> context)
      : T(context, std::make_shared<RNSkMetalCanvasProvider>(
                       std::bind(&RNSkia::RNSkView::requestRedraw, this), context, true)) {}

  CALayer *layer() override { return provider()->getLayer(); }

  void setSize(int width, int height) override { provider()->setSize(width, height); }

  std::shared_ptr<RNSkia::RNSkView> drawView() override { return this->shared_from_this(); }

private:
  std::shared_ptr<RNSkMetalCanvasProvider> provider() {
    return std::static_pointer_cast<RNSkMetalCanvasProvider>(this->getCanvasProvider());
  }
};

} // namespace basalt
