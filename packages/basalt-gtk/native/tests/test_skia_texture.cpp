// The one thing between Skia and the screen on this host: a pixel format.
//
// gtk/GtkSkiaPeer.cpp renders a `<Canvas>` into an `SkImageInfo::MakeN32Premul`
// surface and hands the bytes to `gdk_memory_texture_new` as
// `GDK_MEMORY_B8G8R8A8_PREMULTIPLIED`. That is a claim about two libraries
// agreeing on byte order, made in a comment, and it is the kind of claim that
// fails silently: every red in the app comes out blue, nothing logs, nothing
// crashes, and the bug looks like a Skia problem rather than a layout of bytes.
//
// So this draws known colours through the real path and reads them back. It
// does not test the peer's plumbing -- registration, sizing, the weak
// reference -- which needs a manager and therefore a runtime. It tests the
// assumption underneath all of it.
//
// Only built with Skia; see the CMakeLists.

#include "TestHarness.h"

#include <gtk/gtk.h>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

#include <cstdint>
#include <sstream>
#include <vector>

namespace {

// The peer's conversion, on its own so the test runs the same three calls.
GdkTexture *toTexture(const sk_sp<SkSurface> &surface, int width, int height) {
  SkPixmap pixels;
  if (!surface->peekPixels(&pixels)) {
    return nullptr;
  }
  GBytes *bytes = g_bytes_new(pixels.addr(), pixels.computeByteSize());
  GdkTexture *texture = gdk_memory_texture_new(
      width, height, GDK_MEMORY_B8G8R8A8_PREMULTIPLIED, bytes, pixels.rowBytes());
  g_bytes_unref(bytes);
  return texture;
}

struct Rgba {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  uint8_t alpha = 0;
};

// One pixel out of a downloaded texture. `gdk_texture_download` writes
// GDK_MEMORY_DEFAULT, which is premultiplied BGRA -- so this names the channels
// rather than trusting the reader to remember which byte is which.
Rgba pixelAt(const std::vector<uint8_t> &data, int stride, int x, int y) {
  const uint8_t *p = data.data() + static_cast<size_t>(y) * stride + static_cast<size_t>(x) * 4;
  Rgba out;
  out.blue = p[0];
  out.green = p[1];
  out.red = p[2];
  out.alpha = p[3];
  return out;
}

} // namespace

TEST(skia_a_canvas_surface_becomes_a_texture_of_the_same_colours) {
  constexpr int kWidth = 4;
  constexpr int kHeight = 2;

  sk_sp<SkSurface> surface = SkSurfaces::Raster(SkImageInfo::MakeN32Premul(kWidth, kHeight));
  EXPECT(surface != nullptr);

  SkCanvas *canvas = surface->getCanvas();
  canvas->clear(SK_ColorTRANSPARENT);
  SkPaint paint;
  // Opaque red, and opaque blue beside it. Red alone would pass even if the
  // two libraries disagreed and swapped it with blue -- there would be nothing
  // to notice. Two channels that differ is what makes the swap visible.
  paint.setColor(SK_ColorRED);
  canvas->drawRect(SkRect::MakeXYWH(0, 0, 1, 1), paint);
  paint.setColor(SK_ColorBLUE);
  canvas->drawRect(SkRect::MakeXYWH(1, 0, 1, 1), paint);

  GdkTexture *texture = toTexture(surface, kWidth, kHeight);
  EXPECT(texture != nullptr);
  if (texture == nullptr) {
    return;
  }

  EXPECT_EQ(gdk_texture_get_width(texture), kWidth);
  EXPECT_EQ(gdk_texture_get_height(texture), kHeight);

  const int stride = kWidth * 4;
  std::vector<uint8_t> data(static_cast<size_t>(stride) * kHeight, 0);
  gdk_texture_download(texture, data.data(), static_cast<gsize>(stride));

  const Rgba red = pixelAt(data, stride, 0, 0);
  EXPECT_EQ(static_cast<int>(red.red), 255);
  EXPECT_EQ(static_cast<int>(red.green), 0);
  EXPECT_EQ(static_cast<int>(red.blue), 0);
  EXPECT_EQ(static_cast<int>(red.alpha), 255);

  const Rgba blue = pixelAt(data, stride, 1, 0);
  EXPECT_EQ(static_cast<int>(blue.red), 0);
  EXPECT_EQ(static_cast<int>(blue.green), 0);
  EXPECT_EQ(static_cast<int>(blue.blue), 255);
  EXPECT_EQ(static_cast<int>(blue.alpha), 255);

  // And the part nothing drew stays transparent, which is what lets a canvas
  // sit over the view behind it rather than punching a black hole in it. The
  // renderer clears to transparent before every picture for this reason.
  const Rgba untouched = pixelAt(data, stride, 3, 1);
  EXPECT_EQ(static_cast<int>(untouched.alpha), 0);

  g_object_unref(texture);
}
