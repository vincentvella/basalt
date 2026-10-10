#include "RnWin32Image.h"

#include "Win32Clip.h"

#include <windows.h>

#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
// PropVariantInit and PropVariantClear, for the GIF metadata the query reader
// answers as a PROPVARIANT. Its own header, and not pulled in by wincodec.h.
#include <propidl.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <string>
#include <vector>

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

namespace {

// One unsigned metadata value, or the fallback.
//
// WIC answers a GIF's per-frame metadata through a query reader whose paths are
// the format's own structure: `/grctlext/Delay` is the graphic control
// extension's delay, in hundredths of a second, and `/imgdesc/Left` is the
// image descriptor's offset. A frame that carries no extension has no path to
// read, which is not an error -- it is a frame with no delay of its own.
unsigned metadataValue(IWICMetadataQueryReader *reader, const wchar_t *path, unsigned fallback) {
  if (reader == nullptr) {
    return fallback;
  }
  PROPVARIANT value;
  PropVariantInit(&value);
  unsigned result = fallback;
  if (SUCCEEDED(reader->GetMetadataByName(path, &value))) {
    switch (value.vt) {
      case VT_UI1:
        result = value.bVal;
        break;
      case VT_UI2:
        result = value.uiVal;
        break;
      case VT_UI4:
        result = value.ulVal;
        break;
      case VT_I4:
        result = value.lVal >= 0 ? static_cast<unsigned>(value.lVal) : fallback;
        break;
      default:
        break;
    }
  }
  PropVariantClear(&value);
  return result;
}

// The NETSCAPE2.0 loop count, or 1 for a file that does not carry one.
//
// One rather than zero, which is the measured answer rather than a guess: a GIF
// with no application extension plays once in a browser, and ImageIO reports a
// loop count of 1 for exactly those bytes, which the AppKit suite asserts. The
// count lives in the extension's data block, little-endian, after a one-byte
// sub-block id.
unsigned loopCountOf(IWICBitmapDecoder *decoder) {
  ComPtr<IWICMetadataQueryReader> reader;
  if (FAILED(decoder->GetMetadataQueryReader(reader.GetAddressOf())) || !reader) {
    return 1;
  }

  PROPVARIANT application;
  PropVariantInit(&application);
  bool netscape = false;
  if (SUCCEEDED(reader->GetMetadataByName(L"/appext/application", &application))
      && application.vt == (VT_UI1 | VT_VECTOR) && application.caub.cElems >= 11) {
    const char *const bytes = reinterpret_cast<const char *>(application.caub.pElems);
    netscape = std::string(bytes, 11) == "NETSCAPE2.0";
  }
  PropVariantClear(&application);
  if (!netscape) {
    return 1;
  }

  PROPVARIANT data;
  PropVariantInit(&data);
  unsigned loops = 1;
  if (SUCCEEDED(reader->GetMetadataByName(L"/appext/data", &data))
      && data.vt == (VT_UI1 | VT_VECTOR) && data.caub.cElems >= 4) {
    loops = static_cast<unsigned>(data.caub.pElems[1])
        | (static_cast<unsigned>(data.caub.pElems[2]) << 8);
  }
  PropVariantClear(&data);
  return loops;
}

} // namespace

RnWin32ImageFrames RnWin32Image::framesFromEncodedBytes(const uint8_t *data, size_t size) {
  RnWin32ImageFrames result;
  IWICImagingFactory *factory = wicFactory();
  if (factory == nullptr || data == nullptr || size == 0) {
    return result;
  }

  ComPtr<IWICStream> stream;
  if (FAILED(factory->CreateStream(stream.GetAddressOf()))
      || FAILED(stream->InitializeFromMemory(const_cast<BYTE *>(data),
                                             static_cast<DWORD>(size)))) {
    return result;
  }

  ComPtr<IWICBitmapDecoder> decoder;
  if (FAILED(factory->CreateDecoderFromStream(
          stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf()))) {
    return result;
  }

  UINT count = 0;
  if (FAILED(decoder->GetFrameCount(&count)) || count < 2) {
    return result;
  }

  // The logical screen, which is the canvas every frame is composited onto and
  // is not always the first frame's size.
  ComPtr<IWICMetadataQueryReader> fileReader;
  decoder->GetMetadataQueryReader(fileReader.GetAddressOf());
  unsigned canvasWidth = metadataValue(fileReader.Get(), L"/logscrdesc/Width", 0);
  unsigned canvasHeight = metadataValue(fileReader.Get(), L"/logscrdesc/Height", 0);

  std::vector<uint8_t> canvas;
  std::vector<uint8_t> saved;
  std::vector<unsigned> delays;
  std::vector<std::shared_ptr<RnWin32Image>> frames;

  for (UINT index = 0; index < count; index++) {
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(index, frame.GetAddressOf()))) {
      return {};
    }

    UINT frameWidth = 0;
    UINT frameHeight = 0;
    if (FAILED(frame->GetSize(&frameWidth, &frameHeight)) || frameWidth == 0) {
      return {};
    }
    if (canvasWidth == 0 || canvasHeight == 0) {
      // A format with no logical screen: the first frame is the canvas, which
      // is what a multi-page TIFF or an APNG without one amounts to.
      canvasWidth = frameWidth;
      canvasHeight = frameHeight;
    }
    if (canvas.empty()) {
      canvas.assign(static_cast<size_t>(canvasWidth) * canvasHeight * 4, 0);
    }

    ComPtr<IWICMetadataQueryReader> frameReader;
    frame->GetMetadataQueryReader(frameReader.GetAddressOf());
    const unsigned left = metadataValue(frameReader.Get(), L"/imgdesc/Left", 0);
    const unsigned top = metadataValue(frameReader.Get(), L"/imgdesc/Top", 0);
    // Hundredths of a second, which is the format's unit. Unclamped: the clamp
    // is core's, and this is where the file's own answer is read.
    const unsigned delay = metadataValue(frameReader.Get(), L"/grctlext/Delay", 0) * 10;
    const unsigned disposal = metadataValue(frameReader.Get(), L"/grctlext/Disposal", 0);

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.GetAddressOf()))
        || FAILED(converter->Initialize(frame.Get(),
                                        GUID_WICPixelFormat32bppPBGRA,
                                        WICBitmapDitherTypeNone,
                                        nullptr,
                                        0.0,
                                        WICBitmapPaletteTypeCustom))) {
      return {};
    }

    std::vector<uint8_t> pixels(static_cast<size_t>(frameWidth) * frameHeight * 4, 0);
    const UINT stride = frameWidth * 4;
    if (FAILED(converter->CopyPixels(
            nullptr, stride, static_cast<UINT>(pixels.size()), pixels.data()))) {
      return {};
    }

    // Disposal 3 restores what was there before this frame, so the canvas has
    // to be remembered before it is drawn on.
    if (disposal == 3) {
      saved = canvas;
    }

    // Composited rather than copied: a frame's transparent pixels leave what is
    // under them alone, which is the whole of how a GIF encodes "only this part
    // changed".
    for (unsigned y = 0; y < frameHeight; y++) {
      const unsigned canvasY = top + y;
      if (canvasY >= canvasHeight) {
        break;
      }
      for (unsigned x = 0; x < frameWidth; x++) {
        const unsigned canvasX = left + x;
        if (canvasX >= canvasWidth) {
          break;
        }
        const size_t from = (static_cast<size_t>(y) * frameWidth + x) * 4;
        const size_t to = (static_cast<size_t>(canvasY) * canvasWidth + canvasX) * 4;
        if (pixels[from + 3] == 0) {
          continue;
        }
        for (int channel = 0; channel < 4; channel++) {
          canvas[to + channel] = pixels[from + channel];
        }
      }
    }

    auto composited = fromPixels(canvasWidth, canvasHeight, canvas.data());
    if (composited == nullptr) {
      return {};
    }
    frames.push_back(std::move(composited));
    delays.push_back(delay);

    // What the *next* frame starts from. 0 and 1 leave the canvas alone, 2
    // clears this frame's rectangle back to transparent, and 3 puts back what
    // was there before.
    if (disposal == 2) {
      for (unsigned y = 0; y < frameHeight; y++) {
        const unsigned canvasY = top + y;
        if (canvasY >= canvasHeight) {
          break;
        }
        for (unsigned x = 0; x < frameWidth; x++) {
          const unsigned canvasX = left + x;
          if (canvasX >= canvasWidth) {
            break;
          }
          const size_t to = (static_cast<size_t>(canvasY) * canvasWidth + canvasX) * 4;
          for (int channel = 0; channel < 4; channel++) {
            canvas[to + channel] = 0;
          }
        }
      }
    } else if (disposal == 3 && !saved.empty()) {
      canvas = saved;
    }
  }

  result.frames = std::move(frames);
  result.delaysMs = std::move(delays);
  result.loopCount = loopCountOf(decoder.Get());
  return result;
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
                        const float *tint,
                        float blurRadius) const {
  if (target == nullptr || bitmap_ == nullptr || boxWidth <= 0.0f || boxHeight <= 0.0f) {
    return;
  }

  // `blurRadius`, which is an effect, and an effect needs a device context
  // where this is handed a render target. A target made by a Direct2D 1.1
  // factory answers for one and both of this host's are; the text shadow in
  // RnWin32TextLayout.cpp asks the same question for the same reason.
  //
  // **In the view's coordinates, not the image's pixels**, which is the rule
  // the other two hosts measured rather than chose: blurring in the source's
  // pixels makes one prop almost invisible on a photograph and overwhelming on
  // an icon. So the image is drawn into a bitmap the size of the box, by this
  // same function with no radius, and the blur is applied to that. Which also
  // means the fit, the tint and the tiling are already accounted for, because
  // the recursive call does all three.
  //
  // Clipped to the box, as the blurred image is on both other hosts: a blur
  // spreads beyond its input, and an `<Image>` never paints outside its own
  // frame.
  if (blurRadius > 0.0f) {
    Microsoft::WRL::ComPtr<ID2D1DeviceContext> context;
    Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> offscreen;
    if (SUCCEEDED(target->QueryInterface(IID_PPV_ARGS(&context)))
        && SUCCEEDED(target->CreateCompatibleRenderTarget(D2D1::SizeF(boxWidth, boxHeight),
                                                          offscreen.GetAddressOf()))
        && offscreen) {
      offscreen->BeginDraw();
      offscreen->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
      draw(offscreen.Get(), boxWidth, boxHeight, fit, tint, 0.0f);
      if (SUCCEEDED(offscreen->EndDraw())) {
        Microsoft::WRL::ComPtr<ID2D1Bitmap> drawn;
        Microsoft::WRL::ComPtr<ID2D1Effect> blur;
        if (SUCCEEDED(offscreen->GetBitmap(drawn.GetAddressOf())) && drawn
            && SUCCEEDED(context->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf()))
            && blur) {
          blur->SetInput(0, drawn.Get());
          // Half the radius is the standard deviation on both other hosts,
          // measured there rather than chosen here; see backlog/image.md.
          blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, blurRadius / 2.0f);
          const ScopedGeometryClip clip(target,
                                        D2D1::RectF(0.0f, 0.0f, boxWidth, boxHeight),
                                        0.0f);
          context->DrawImage(blur.Get(), D2D1::Point2F(0.0f, 0.0f));
          return;
        }
      }
    }
    // No device context and no effect: the sharp image is the honest fallback,
    // since a prop that cannot be honoured should not take the picture with it.
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
