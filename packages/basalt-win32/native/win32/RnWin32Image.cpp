#include "RnWin32Image.h"

#include "Win32Clip.h"

#include <windows.h>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace basalt::win32 {
namespace {

bool ensureCom() {
  const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

IWICImagingFactory *wicFactory() {
  // Every call, not only the first: decoding happens on whichever worker thread
  // the image loader put it on, and COM is per-thread. The factory itself is
  // agile -- WIC registers it "Both" -- so one instance serves every thread,
  // but a thread that has never called CoInitializeEx cannot use it.
  // CoInitializeEx on an already-initialised thread returns S_FALSE and costs
  // nothing.
  if (!ensureCom()) {
    return nullptr;
  }
  static IWICImagingFactory *factory = [] {
    IWICImagingFactory *created = nullptr;
    CoCreateInstance(
        CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&created));
    return created;
  }();
  return factory;
}

} // namespace

const char *imageFitName(RnImageFit fit) {
  switch (fit) {
    case RnImageFit::Contain:
      return "contain";
    case RnImageFit::Stretch:
      return "stretch";
    case RnImageFit::Center:
      return "center";
    case RnImageFit::Repeat:
      return "repeat";
    case RnImageFit::Cover:
      break;
  }
  return "cover";
}

std::shared_ptr<RnWin32Image>
RnWin32Image::fromPixels(unsigned width, unsigned height, const uint8_t *bgra) {
  IWICImagingFactory *factory = wicFactory();
  if (factory == nullptr || bgra == nullptr || width == 0 || height == 0) {
    return nullptr;
  }

  const UINT stride = width * 4;
  ComPtr<IWICBitmap> bitmap;
  if (FAILED(factory->CreateBitmapFromMemory(width,
                                             height,
                                             GUID_WICPixelFormat32bppPBGRA,
                                             stride,
                                             stride * height,
                                             const_cast<BYTE *>(bgra),
                                             bitmap.GetAddressOf()))) {
    return nullptr;
  }

  auto image = std::shared_ptr<RnWin32Image>(new RnWin32Image());
  image->bitmap_ = bitmap.Detach();
  image->width_ = width;
  image->height_ = height;
  return image;
}

std::shared_ptr<RnWin32Image> RnWin32Image::fromEncodedBytes(const uint8_t *data, size_t size) {
  IWICImagingFactory *factory = wicFactory();
  if (factory == nullptr || data == nullptr || size == 0) {
    return nullptr;
  }

  // A stream over the caller's bytes. WIC copies what it needs during
  // CreateBitmapFromSource below, so the caller's buffer does not have to
  // outlive this function.
  ComPtr<IWICStream> stream;
  if (FAILED(factory->CreateStream(stream.GetAddressOf())) ||
      FAILED(stream->InitializeFromMemory(const_cast<BYTE *>(data), static_cast<DWORD>(size)))) {
    return nullptr;
  }

  ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(factory->CreateDecoderFromStream(
          stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) {
    return nullptr;
  }

  ComPtr<IWICBitmapFrameDecode> frame;
  if (FAILED(decoder->GetFrame(0, frame.GetAddressOf()))) {
    return nullptr;
  }

  // Whatever the file was, Direct2D wants premultiplied BGRA.
  ComPtr<IWICFormatConverter> converter;
  if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf())) ||
      FAILED(converter->Initialize(frame.Get(),
                                   GUID_WICPixelFormat32bppPBGRA,
                                   WICBitmapDitherTypeNone,
                                   nullptr,
                                   0.0,
                                   WICBitmapPaletteTypeCustom))) {
    return nullptr;
  }

  ComPtr<IWICBitmap> bitmap;
  if (FAILED(factory->CreateBitmapFromSource(
          converter.Get(), WICBitmapCacheOnLoad, bitmap.GetAddressOf()))) {
    return nullptr;
  }

  UINT width = 0;
  UINT height = 0;
  if (FAILED(bitmap->GetSize(&width, &height)) || width == 0 || height == 0) {
    return nullptr;
  }

  auto image = std::shared_ptr<RnWin32Image>(new RnWin32Image());
  image->bitmap_ = bitmap.Detach();
  image->width_ = width;
  image->height_ = height;
  return image;
}

RnWin32Image::~RnWin32Image() {
  if (deviceBitmap_ != nullptr) {
    deviceBitmap_->Release();
  }
  if (bitmap_ != nullptr) {
    bitmap_->Release();
  }
}

void RnWin32Image::draw(ID2D1RenderTarget *target,
                        float boxWidth,
                        float boxHeight,
                        RnImageFit fit,
                        const float *tint) const {
  if (target == nullptr || bitmap_ == nullptr || boxWidth <= 0.0f || boxHeight <= 0.0f) {
    return;
  }

  if (deviceBitmap_ == nullptr || deviceTarget_ != target) {
    if (deviceBitmap_ != nullptr) {
      deviceBitmap_->Release();
      deviceBitmap_ = nullptr;
    }
    ID2D1Bitmap *created = nullptr;
    if (FAILED(target->CreateBitmapFromWicBitmap(bitmap_, nullptr, &created))) {
      return;
    }
    deviceBitmap_ = created;
    deviceTarget_ = target;
  }

  const float imageWidth = static_cast<float>(width_);
  const float imageHeight = static_cast<float>(height_);

  // The same arithmetic rn_view_snapshot does on GTK, in the same order, so the
  // three desktops crop and centre identically. A picture is the only thing
  // that would catch a divergence here, which is why test_win32_image.cpp reads
  // pixels rather than checking the destination rect.
  D2D1_RECT_F destination = D2D1::RectF(0.0f, 0.0f, boxWidth, boxHeight);
  if (fit != RnImageFit::Stretch) {
    float scale = 1.0f;
    switch (fit) {
      case RnImageFit::Contain:
        scale = std::min(boxWidth / imageWidth, boxHeight / imageHeight);
        break;
      case RnImageFit::Cover:
        scale = std::max(boxWidth / imageWidth, boxHeight / imageHeight);
        break;
      case RnImageFit::Center:
        // Centred at natural size, but never larger than the frame -- which is
        // what React Native's `center` does.
        scale = std::min(1.0f, std::min(boxWidth / imageWidth, boxHeight / imageHeight));
        break;
      case RnImageFit::Repeat:
        // One tile, at natural size. Where the tiles go is decided below.
        break;
      case RnImageFit::Stretch:
        break;
    }
    const float drawnWidth = imageWidth * scale;
    const float drawnHeight = imageHeight * scale;
    // From the top left for `repeat`, which is what CSS does: the whole tiles
    // start at the origin and the partial one is at the far edge.
    const float x = fit == RnImageFit::Repeat ? 0.0f : (boxWidth - drawnWidth) / 2.0f;
    const float y = fit == RnImageFit::Repeat ? 0.0f : (boxHeight - drawnHeight) / 2.0f;
    destination = D2D1::RectF(x, y, x + drawnWidth, y + drawnHeight);
  }

  // cover and center can put pixels outside the frame, and an <Image> never
  // paints beyond its own box on iOS or Android. A geometry clip rather than an
  // axis-aligned one, because an <Image> inside a rotated view is not exotic;
  // see Win32Clip.h.
  const bool needsClip =
      fit == RnImageFit::Cover || fit == RnImageFit::Center || fit == RnImageFit::Repeat;
  {
    const ScopedGeometryClip clip(needsClip ? target : nullptr,
                                  D2D1::RectF(0.0f, 0.0f, boxWidth, boxHeight),
                                  0.0f);
    if (fit == RnImageFit::Repeat) {
      // A bitmap brush in wrap mode, which is Direct2D's tiling: the brush
      // repeats the bitmap across whatever is filled, and the clip above is
      // what stops it at the view's edge.
      //
      // Nearest-neighbour on purpose. The default is linear, which blends the
      // last column of one tile into the first of the next and leaves a seam
      // on every boundary -- visible on exactly the small, sharp images people
      // tile.
      Microsoft::WRL::ComPtr<ID2D1BitmapBrush> brush;
      const D2D1_BITMAP_BRUSH_PROPERTIES properties = D2D1::BitmapBrushProperties(
          D2D1_EXTEND_MODE_WRAP,
          D2D1_EXTEND_MODE_WRAP,
          D2D1_BITMAP_INTERPOLATION_MODE_NEAREST_NEIGHBOR);
      // The four-argument form on the interface itself rather than one of
      // d2d1.h's inline reference overloads, so which one is selected is not a
      // thing to be wrong about.
      if (SUCCEEDED(target->CreateBitmapBrush(
              deviceBitmap_, &properties, nullptr, brush.GetAddressOf()))) {
        target->FillRectangle(D2D1::RectF(0.0f, 0.0f, boxWidth, boxHeight), brush.Get());
      }
    } else if (tint != nullptr) {
      // The bitmap becomes an opacity mask and the brush is what is painted.
      // Direct2D requires aliased antialiasing for a mask whose content is
      // graphics rather than text, and refuses the call otherwise -- so the
      // mode is changed for the one draw and put back.
      Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> brush;
      const D2D1_COLOR_F color = D2D1::ColorF(tint[0], tint[1], tint[2], tint[3]);
      if (SUCCEEDED(target->CreateSolidColorBrush(color, brush.GetAddressOf()))) {
        const D2D1_ANTIALIAS_MODE previous = target->GetAntialiasMode();
        target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
        target->FillOpacityMask(deviceBitmap_,
                                brush.Get(),
                                D2D1_OPACITY_MASK_CONTENT_GRAPHICS,
                                &destination,
                                nullptr);
        target->SetAntialiasMode(previous);
      }
    } else {
      target->DrawBitmap(deviceBitmap_, destination);
    }
  }
}

} // namespace basalt::win32
