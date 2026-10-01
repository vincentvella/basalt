// That Skia actually renders on this host, rather than merely linking.
//
// `RNSkiaModule: Skia installed` says the bindings were constructed. It does not
// say a pixel can be drawn, and the two are a long way apart: a Metal device that
// fails to create, a GrDirectContext that comes back null, a surface that wraps a
// texture nothing can read -- each of those installs perfectly well and then
// produces nothing. So this draws a known shape into an offscreen surface and
// reads the pixels back out.
//
// Against real pixels rather than against the objects returned, for the reason
// test_appkit_image.mm gives about resize modes: a call that returned a plausible
// surface and drew nowhere would pass either way. What is asserted is where the
// ink landed.
//
// Only compiled when the build was pointed at an app that has the package, so
// these run on a developer's machine and not in CI -- which has no app and so no
// Skia. See ../../basalt-core/native/cmake/Skia.cmake.

#include "TestHarness.h"

#include "AppKitSkiaContext.h"

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"
#include "include/gpu/ganesh/GrDirectContext.h"

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int kSize = 64;

// The context as the module builds it, minus the call invoker. Nothing here
// crosses to the JavaScript thread, so a null one is honest: it would be used by
// runOnJavascriptThread, which no test below reaches.
std::shared_ptr<RNSkia::RNSkPlatformContext> context() {
  static std::shared_ptr<RNSkia::RNSkPlatformContext> shared =
      basalt::makeSkiaPlatformContext(nullptr);
  return shared;
}

// One pixel, read back from a GPU surface. Premultiplied BGRA is what
// MakeOffscreen's texture is, and readPixels converts into whatever is asked
// for -- RGBA here, so the assertions below read in the order the names suggest.
struct Pixel {
  uint8_t r = 0, g = 0, b = 0, a = 0;

  std::string describe() const {
    std::ostringstream out;
    out << "rgba(" << (int)r << "," << (int)g << "," << (int)b << "," << (int)a << ")";
    return out.str();
  }
};

std::vector<Pixel> readBack(const sk_sp<SkSurface> &surface) {
  sk_sp<SkImage> snapshot = surface->makeImageSnapshot();
  if (snapshot == nullptr) {
    return {};
  }
  const SkImageInfo info =
      SkImageInfo::Make(kSize, kSize, kRGBA_8888_SkColorType, kPremul_SkAlphaType);
  std::vector<Pixel> pixels(static_cast<size_t>(kSize) * kSize);
  // Through the direct context, because the image is texture-backed: the
  // no-context overload returns false for one and the failure looks like an
  // all-zero bitmap, which is also what a surface nothing drew into looks like.
  if (!snapshot->readPixels(context()->getDirectContext(), info, pixels.data(),
                            kSize * sizeof(Pixel), 0, 0)) {
    return {};
  }
  return pixels;
}

const Pixel &at(const std::vector<Pixel> &pixels, int x, int y) {
  return pixels[static_cast<size_t>(y) * kSize + x];
}

} // namespace

TEST(skia_has_a_gpu_context) {
  // If this is null nothing below can work, and the reason is a Metal device
  // that would not create rather than anything about Skia.
  EXPECT(context()->getDirectContext() != nullptr);
}

TEST(skia_makes_an_offscreen_surface) {
  // The call an app renders frames for export through; see kino's
  // export/renderFrame.tsx, which is Skia.Surface.MakeOffscreen.
  sk_sp<SkSurface> surface = context()->makeOffscreenSurface(kSize, kSize);
  EXPECT(surface != nullptr);
  EXPECT_EQ(surface->width(), kSize);
  EXPECT_EQ(surface->height(), kSize);
}

TEST(skia_draws_a_rectangle_where_it_was_asked_to) {
  sk_sp<SkSurface> surface = context()->makeOffscreenSurface(kSize, kSize);
  EXPECT(surface != nullptr);

  SkCanvas *canvas = surface->getCanvas();
  canvas->clear(SK_ColorTRANSPARENT);
  SkPaint paint;
  paint.setColor(SK_ColorRED);
  // A quarter of the surface, offset from the origin so that "drew something"
  // and "drew it in the right place" are different assertions.
  canvas->drawRect(SkRect::MakeXYWH(16, 16, 32, 32), paint);

  const std::vector<Pixel> pixels = readBack(surface);
  EXPECT(!pixels.empty());
  if (pixels.empty()) {
    return;
  }

  // Inside: red, opaque.
  const Pixel &inside = at(pixels, 32, 32);
  EXPECT_EQ(inside.describe(), std::string("rgba(255,0,0,255)"));

  // Outside, well clear of the edge: nothing was drawn, so nothing is there.
  // Not tested right at the boundary -- antialiasing puts an edge half a pixel
  // either way, and pinning that fails on a future Skia for no useful reason.
  const Pixel &outside = at(pixels, 4, 4);
  EXPECT_EQ(outside.describe(), std::string("rgba(0,0,0,0)"));
}

TEST(skia_draws_a_path) {
  // Paths are the other half of what an app actually uses -- kino imports Path,
  // Rect, RoundedRect and RadialGradient -- and they go through a different
  // part of Skia than a rectangle fill.
  sk_sp<SkSurface> surface = context()->makeOffscreenSurface(kSize, kSize);
  EXPECT(surface != nullptr);

  SkCanvas *canvas = surface->getCanvas();
  canvas->clear(SK_ColorTRANSPARENT);
  SkPaint paint;
  paint.setColor(SK_ColorGREEN);
  paint.setAntiAlias(false);
  // A triangle filling the lower left half, so the two diagonal corners differ.
  //
  // Through SkPathBuilder: this Skia has no SkPath::moveTo. Worth knowing when
  // porting anything that touches paths -- the mutating API moved and the old
  // one is gone rather than deprecated.
  SkPathBuilder builder;
  builder.moveTo(0, 0);
  builder.lineTo(0, kSize);
  builder.lineTo(kSize, kSize);
  builder.close();
  canvas->drawPath(builder.detach(), paint);

  const std::vector<Pixel> pixels = readBack(surface);
  EXPECT(!pixels.empty());
  if (pixels.empty()) {
    return;
  }
  // Inside the triangle, and outside it. The corners rather than near the
  // hypotenuse, for the antialiasing reason above.
  EXPECT_EQ(at(pixels, 4, 60).describe(), std::string("rgba(0,255,0,255)"));
  EXPECT_EQ(at(pixels, 60, 4).describe(), std::string("rgba(0,0,0,0)"));
}

TEST(skia_finds_the_system_fonts) {
  // CoreText through Skia's own font manager, which is what Text and matchFont
  // resolve against. A manager with no families is the shape this fails in.
  sk_sp<SkFontMgr> fonts = context()->createFontMgr();
  EXPECT(fonts != nullptr);
  EXPECT(fonts->countFamilies() > 0);

  // And the host's own list, which the package asks for by a different route.
  EXPECT(!context()->getSystemFontFamilies().empty());
}

TEST(skia_says_which_calls_it_does_not_implement) {
  // The three that throw do so with their own name in the message. Asserted
  // because the alternative -- a blank image or a zero handle -- reads as a
  // rendering bug rather than as a missing feature, which is the whole reason
  // they throw.
  bool threw = false;
  std::string message;
  try {
    context()->takeScreenshotFromViewTag(7);
  } catch (const std::exception &error) {
    threw = true;
    message = error.what();
  }
  EXPECT(threw);
  EXPECT(message.find("not implemented") != std::string::npos);
  EXPECT(message.find("7") != std::string::npos);

  threw = false;
  try {
    context()->makeTestNativeBuffer(8, 8);
  } catch (const std::exception &error) {
    threw = true;
    message = error.what();
  }
  EXPECT(threw);
  EXPECT(message.find("CVPixelBuffer") != std::string::npos);

  // And the one that deliberately does not throw: releasing a buffer nothing
  // handed out must be quiet, because throwing from a release path turns a leak
  // into a crash.
  context()->releaseNativeBuffer(0);
}
