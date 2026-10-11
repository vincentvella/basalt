// Skia, compiled and linked into this platform's own suite.
//
// The first question under every part of porting `@shopify/react-native-skia`
// is whether the Skia an app installs can be built against here at all: its
// headers want a C++20 compiler and its prebuilt library is an Apple static
// archive aimed at an Xcode build. Everything after this -- the module, the
// platform context, a `<Canvas>` -- assumes the answer is yes, so the answer
// is measured here rather than assumed.
//
// Drawing rather than linking alone, and reading the pixel back: a library
// that links and draws nothing is the failure this would otherwise miss. The
// surface is a raster one, which needs no window, no Metal device and no
// display -- so this runs on a machine with none of them, which is what CI is.

#include "TestHarness.h"

// EXPECT_EQ formats through a stringstream, which the harness does not include.
#include <sstream>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

TEST(skia_draws_into_a_raster_surface) {
  sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(64, 64));
  EXPECT(surface != nullptr);
  if (surface == nullptr) {
    return;
  }
  EXPECT_EQ(surface->width(), 64);
  EXPECT_EQ(surface->height(), 64);

  SkPaint paint;
  paint.setColor(SK_ColorRED);
  surface->getCanvas()->clear(SK_ColorTRANSPARENT);
  surface->getCanvas()->drawRect(SkRect::MakeXYWH(8, 8, 32, 32), paint);

  // The pixels themselves, which is the half a link check cannot answer: a
  // Skia that draws nothing links exactly as well as one that draws.
  SkPixmap pixels;
  EXPECT(surface->peekPixels(&pixels));
  if (!pixels.addr()) {
    return;
  }
  // Inside the rectangle, and outside it.
  EXPECT_EQ(static_cast<int>(SkColorGetR(pixels.getColor(16, 16))), 255);
  EXPECT_EQ(static_cast<int>(SkColorGetA(pixels.getColor(16, 16))), 255);
  EXPECT_EQ(static_cast<int>(SkColorGetA(pixels.getColor(56, 56))), 0);
}
