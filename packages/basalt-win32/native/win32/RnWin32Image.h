// The decoded pixels of an <Image>, and how they fill a frame.
//
// Device-independent on purpose. Direct2D's own ID2D1Bitmap belongs to the
// render target that made it, so holding one would tie an image to a window and
// make it useless to the offscreen snapshot -- and would have to be rebuilt
// whenever a device is lost. A WIC bitmap is the Windows equivalent of GTK's
// GdkTexture and macOS's CGImage: decoded once, owned by nobody in particular,
// converted to a device bitmap at paint time and cached there.
//
// Only the decode and the draw are here. Fetching the bytes -- file, data URI,
// http -- is `core/ImageBytes.cpp` in the shared half, because a URI means the
// same thing on every desktop; see `docs/DECISIONS.md`.

#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

struct ID2D1RenderTarget;
struct IWICBitmap;
struct ID2D1Bitmap;

namespace basalt::win32 {

// How an image fills its frame. Mirrors React Native's ImageResizeMode, minus
// Repeat, which needs a tiled draw rather than one image draw.
enum class RnImageFit {
  Cover,
  Contain,
  Stretch,
  Center,
  // Tiled from the top left at the image's own size, which is what CSS
  // `repeat` and React Native's `resizeMode: 'repeat'` mean.
  Repeat,
};

const char *imageFitName(RnImageFit fit);

class RnWin32Image;

// Every frame of an animated image, with the delay each is shown for and how
// many times round.
//
// The frames are composited: a GIF's frames are often smaller than its canvas
// and carry a disposal method, where ImageIO and gdk-pixbuf hand over whole
// frames, so this host does that work itself and hands over whole frames too.
// The delays are the file's own, unclamped, because `core/ImageAnimation.h`
// owns the clamp for all three hosts.
struct RnWin32ImageFrames {
  std::vector<std::shared_ptr<RnWin32Image>> frames;
  std::vector<unsigned> delaysMs;
  // Zero is forever, which is what a looping GIF's NETSCAPE2.0 extension says.
  // A file with no extension at all plays once, which is what ImageIO reports
  // for the same bytes and what the other two hosts do.
  unsigned loopCount = 1;

  bool animated() const { return frames.size() > 1 && delaysMs.size() == frames.size(); }
};

class RnWin32Image {
 public:
  // Straight from pixels, premultiplied BGRA with a stride of width * 4. What
  // the tests use, and what a decoder that is not WIC would hand over.
  static std::shared_ptr<RnWin32Image>
  fromPixels(unsigned width, unsigned height, const uint8_t *bgra);

  // Decodes PNG, JPEG, GIF, BMP, TIFF and anything else WIC has a codec for.
  // Null if the bytes are not an image WIC understands.
  static std::shared_ptr<RnWin32Image> fromEncodedBytes(const uint8_t *data, size_t size);

  // The same bytes as an animation, or an empty result for a file with one
  // frame. Decoded separately from `fromEncodedBytes` and not instead of it:
  // every still image goes through that one, and a second decode costs only
  // the frames a still image does not have.
  static RnWin32ImageFrames framesFromEncodedBytes(const uint8_t *data, size_t size);

  ~RnWin32Image();

  RnWin32Image(const RnWin32Image &) = delete;
  RnWin32Image &operator=(const RnWin32Image &) = delete;

  unsigned width() const { return width_; }
  unsigned height() const { return height_; }

  // Draws into a box `boxWidth` by `boxHeight` at the target's current origin,
  // scaled and positioned by `fit`.
  // `tint`, when given, recolours the image keeping its alpha -- one silhouette
  // asset drawn in any colour, which is what `tintColor` is for. Four floats,
  // red green blue alpha, which is what the caller already holds.
  //
  // Not a `D2D1_COLOR_F`, though that is what it becomes: this header declares
  // its Direct2D types rather than including d2d1.h, and that one is a typedef
  // of a typedef -- `D3DCOLORVALUE`, itself `_D3DCOLORVALUE` -- so it cannot be
  // forward-declared at all. Declaring `struct D2D_COLOR_F` instead defines a
  // *different* type of that name and every later include of d2d1.h fails on
  // the redefinition, which is exactly what it did. Floats cannot collide with
  // anything, and the colour is built where d2d1.h is already in scope.
  // `blurRadius`, in the view's own coordinates rather than the image's
  // pixels, which is the rule both other hosts measured: the same number has to
  // be the same picture on a photograph and on an icon. Zero or less is no
  // blur. Half the radius is the gaussian's standard deviation, which is what
  // the other two hosts pass and what `CLSID_D2D1GaussianBlur` takes.
  void draw(ID2D1RenderTarget *target,
            float boxWidth,
            float boxHeight,
            RnImageFit fit,
            const float *tint = nullptr,
            float blurRadius = 0.0f) const;

 private:
  RnWin32Image() = default;

  IWICBitmap *bitmap_ = nullptr;
  unsigned width_ = 0;
  unsigned height_ = 0;

  // The device bitmap for the target that last drew this image. An <Image> in a
  // list is drawn into the same target every frame, so this converts once; a
  // second target -- the snapshot -- replaces it rather than growing a map,
  // because nothing here draws into two targets in the same frame.
  mutable ID2D1Bitmap *deviceBitmap_ = nullptr;
  mutable ID2D1RenderTarget *deviceTarget_ = nullptr;
};

} // namespace basalt::win32
