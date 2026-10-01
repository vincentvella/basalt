// The resize modes, against a real bitmap.
//
// The same seven questions tests/test_appkit_image.mm asks of AppKit, and they
// are asked differently here. That suite checks the destination rect the view
// computed; this one renders and reads the pixels, because the arithmetic is
// only half the claim -- `cover` that scales correctly and forgets to clip
// looks right in a rect and wrong on a screen.
//
// The picture is a two-colour image: red on the left half, blue on the right.
// That makes it asymmetric, so a fit that mirrors or transposes is visible, and
// it makes "which part got cropped" answerable.

#include "TestHarness.h"

#include "RnWin32Image.h"
#include "RnWin32View.h"
#include "Win32Snapshot.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <vector>

using basalt::win32::RnImageFit;
using basalt::win32::RnPixel;
using basalt::win32::RnPixels;
using basalt::win32::RnWin32Image;
using basalt::win32::RnWin32View;

namespace {

constexpr double kTolerance = 3.0;

// Premultiplied BGRA, which is what fromPixels documents. Both halves are
// opaque, so premultiplied and straight are the same bytes here.
std::vector<uint8_t> twoTone(unsigned width, unsigned height) {
  std::vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
  for (unsigned y = 0; y < height; y++) {
    for (unsigned x = 0; x < width; x++) {
      const size_t offset = (static_cast<size_t>(y) * width + x) * 4;
      const bool leftHalf = x < width / 2;
      pixels[offset + 0] = leftHalf ? 0 : 255;   // blue
      pixels[offset + 1] = 0;                    // green
      pixels[offset + 2] = leftHalf ? 255 : 0;   // red
      pixels[offset + 3] = 255;                  // alpha
    }
  }
  return pixels;
}

std::shared_ptr<RnWin32Image> twoToneImage(unsigned width, unsigned height) {
  const std::vector<uint8_t> pixels = twoTone(width, height);
  return RnWin32Image::fromPixels(width, height, pixels.data());
}

// Renders one view holding the image in a box, and hands back the pixels.
RnPixels renderInBox(const std::shared_ptr<RnWin32Image> &image,
                     RnImageFit fit,
                     float boxWidth,
                     float boxHeight) {
  auto root = std::make_unique<RnWin32View>(1);
  root->setFrame(0, 0, boxWidth, boxHeight);
  root->setImage(image, fit);
  return basalt::win32::renderToPixels(*root);
}

} // namespace

#define EXPECT_PIXEL(pixels, x, y, r, g, b, a)                                                     \
  do {                                                                                             \
    const RnPixel pixel = (pixels).at((x), (y));                                                   \
    EXPECT_NEAR(pixel.red, (r), kTolerance);                                                       \
    EXPECT_NEAR(pixel.green, (g), kTolerance);                                                     \
    EXPECT_NEAR(pixel.blue, (b), kTolerance);                                                      \
    EXPECT_NEAR(pixel.alpha, (a), kTolerance);                                                     \
  } while (false)

#define EXPECT_TRANSPARENT(pixels, x, y) EXPECT_NEAR((pixels).at((x), (y)).alpha, 0, kTolerance)

TEST(image_decodes_from_pixels) {
  auto image = twoToneImage(40, 20);
  EXPECT(image != nullptr);
  EXPECT_EQ(image->width(), 40u);
  EXPECT_EQ(image->height(), 20u);
}

TEST(image_stretch_fills_the_frame) {
  auto image = twoToneImage(40, 20);
  const RnPixels pixels = renderInBox(image, RnImageFit::Stretch, 200, 100);
  EXPECT(!pixels.empty());

  // Distorted to the whole box, so every corner is covered and the halves land
  // where the aspect ratio says they should not.
  EXPECT_PIXEL(pixels, 50, 50, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 150, 50, 0, 0, 255, 255);
  EXPECT_PIXEL(pixels, 5, 5, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 195, 95, 0, 0, 255, 255);
}

TEST(image_contain_fits_inside_and_centres) {
  // A 40x20 image in a 200x200 box: contain scales by 5, giving 200x100
  // centred, so the top and bottom fifty rows are empty.
  auto image = twoToneImage(40, 20);
  const RnPixels pixels = renderInBox(image, RnImageFit::Contain, 200, 200);
  EXPECT(!pixels.empty());

  EXPECT_TRANSPARENT(pixels, 100, 10);
  EXPECT_TRANSPARENT(pixels, 100, 190);
  EXPECT_PIXEL(pixels, 50, 100, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 150, 100, 0, 0, 255, 255);
}

TEST(image_cover_fills_the_frame_and_crops) {
  // The same image in the same box: cover scales by 10, giving 400x200, so the
  // sides are cropped and nothing is empty.
  auto image = twoToneImage(40, 20);
  const RnPixels pixels = renderInBox(image, RnImageFit::Cover, 200, 200);
  EXPECT(!pixels.empty());

  // Top and bottom are covered, which is the difference from `contain` -- and
  // sampled away from x=100, because the seam between the two halves lands
  // exactly there and the sample would read the interpolation across it. That
  // is not a nicety: the first version of this test asked at x=100 and got
  // (115,0,140), which is a real answer to the wrong question.
  EXPECT_PIXEL(pixels, 50, 10, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 50, 190, 255, 0, 0, 255);
  // The seam is at the image's centre, which cover keeps at the box's centre --
  // so the left of the box is red and the right is blue, with the outer
  // quarters of the image cropped away.
  EXPECT_PIXEL(pixels, 20, 100, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 180, 100, 0, 0, 255, 255);
}

TEST(image_cover_does_not_paint_outside_the_frame) {
  // The clip is the half of `cover` a destination rect cannot show. The image
  // is drawn 400 wide inside a 200-wide view, so without the clip it would
  // reach the surrounding root.
  auto image = twoToneImage(40, 20);

  auto root = std::make_unique<RnWin32View>(1);
  root->setFrame(0, 0, 400, 200);
  auto framed = std::make_unique<RnWin32View>(2);
  framed->setFrame(100, 0, 200, 200);
  framed->setImage(image, RnImageFit::Cover);
  root->insertChild(framed.get(), 0);

  const RnPixels pixels = basalt::win32::renderToPixels(*root);
  EXPECT(!pixels.empty());

  EXPECT_PIXEL(pixels, 150, 100, 255, 0, 0, 255);
  EXPECT_TRANSPARENT(pixels, 50, 100);
  EXPECT_TRANSPARENT(pixels, 350, 100);
}

TEST(image_center_draws_at_natural_size) {
  // 40x20 in a 200x200 box, centred and unscaled: it occupies x 80..120,
  // y 90..110.
  auto image = twoToneImage(40, 20);
  const RnPixels pixels = renderInBox(image, RnImageFit::Center, 200, 200);
  EXPECT(!pixels.empty());

  EXPECT_PIXEL(pixels, 90, 100, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 110, 100, 0, 0, 255, 255);
  EXPECT_TRANSPARENT(pixels, 60, 100);
  EXPECT_TRANSPARENT(pixels, 100, 70);
}

TEST(image_center_shrinks_an_oversized_image) {
  // Bigger than the box, so `center` behaves like `contain` rather than
  // overflowing: 400x200 into 200x200 scales by a half to 200x100, centred.
  auto image = twoToneImage(400, 200);
  const RnPixels pixels = renderInBox(image, RnImageFit::Center, 200, 200);
  EXPECT(!pixels.empty());

  EXPECT_PIXEL(pixels, 50, 100, 255, 0, 0, 255);
  EXPECT_PIXEL(pixels, 150, 100, 0, 0, 255, 255);
  EXPECT_TRANSPARENT(pixels, 100, 10);
}

TEST(image_is_reported_in_the_tree) {
  auto view = std::make_unique<RnWin32View>(9);
  view->setFrame(0, 0, 64, 64);
  view->setImage(twoToneImage(40, 20), RnImageFit::Contain);

  EXPECT_EQ(view->describeTree(),
            std::string("view tag=9 frame=(0,0 64x64) texture=40x20 fit=contain\n"));
}

TEST(image_can_be_cleared) {
  auto view = std::make_unique<RnWin32View>(9);
  view->setFrame(0, 0, 64, 64);
  view->setImage(twoToneImage(40, 20), RnImageFit::Cover);
  view->setImage(nullptr, RnImageFit::Cover);

  EXPECT(view->image() == nullptr);
  EXPECT_EQ(view->describeTree(), std::string("view tag=9 frame=(0,0 64x64)\n"));
}

TEST(image_decodes_an_encoded_png) {
  // A round trip through the two halves of this platform's image handling: the
  // snapshot encoder writes a PNG, and WIC reads it back. Cheaper than checking
  // in a fixture, and it fails if either half breaks.
  auto source = std::make_unique<RnWin32View>(1);
  source->setFrame(0, 0, 24, 12);
  source->setBackgroundColor(0.0f, 1.0f, 0.0f, 1.0f, true);

  const std::string path = std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") +
                           "\\basalt_win32_roundtrip.png";
  EXPECT(basalt::win32::writeSnapshot(*source, path));

  std::vector<uint8_t> encoded;
  if (FILE *file = std::fopen(path.c_str(), "rb")) {
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    encoded.resize(static_cast<size_t>(size > 0 ? size : 0));
    if (!encoded.empty()) {
      const size_t read = std::fread(encoded.data(), 1, encoded.size(), file);
      encoded.resize(read);
    }
    std::fclose(file);
  }
  EXPECT(!encoded.empty());

  auto decoded = RnWin32Image::fromEncodedBytes(encoded.data(), encoded.size());
  EXPECT(decoded != nullptr);
  if (decoded != nullptr) {
    EXPECT_EQ(decoded->width(), 24u);
    EXPECT_EQ(decoded->height(), 12u);
  }
  std::remove(path.c_str());
}

TEST(image_rejects_bytes_that_are_not_an_image) {
  const uint8_t rubbish[] = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'};
  EXPECT(RnWin32Image::fromEncodedBytes(rubbish, sizeof(rubbish)) == nullptr);
  EXPECT(RnWin32Image::fromEncodedBytes(nullptr, 0) == nullptr);
}
