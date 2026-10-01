#include "Win32Snapshot.h"

#include "RnWin32View.h"

#include <windows.h>

#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cmath>
#include <string>

using Microsoft::WRL::ComPtr;

namespace basalt::win32 {
namespace {

// COM, initialised once per thread and deliberately never uninitialised.
//
// A snapshot can be taken from a thread that already called CoInitializeEx --
// the host's main thread will have -- and RPC_E_CHANGED_MODE says exactly that
// and is not a failure. Balancing this with CoUninitialize would tear down the
// apartment the caller set up, so it is left alone: nothing here owns the
// thread it is called on.
bool ensureCom() {
  const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
  return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
}

std::wstring widen(const std::string &text) {
  if (text.empty()) {
    return {};
  }
  const int needed =
      MultiByteToWideChar(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0);
  if (needed <= 0) {
    return {};
  }
  std::wstring wide(static_cast<size_t>(needed), L'\0');
  MultiByteToWideChar(
      CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), wide.data(), needed);
  return wide;
}

// Renders the tree into a WIC bitmap. The one place the render is described, so
// the pixels a test reads and the pixels a PNG carries cannot come out
// different -- the same reason the text layout is built in one place and shared
// by measurement and painting.
bool renderToBitmap(const RnWin32View &root,
                    ComPtr<IWICImagingFactory> &wic,
                    ComPtr<IWICBitmap> &bitmap,
                    UINT &width,
                    UINT &height) {
  const RnRect frame = root.frame();
  width = static_cast<UINT>(std::lround(frame.width));
  height = static_cast<UINT>(std::lround(frame.height));
  if (width == 0 || height == 0) {
    return false;
  }

  if (!ensureCom()) {
    return false;
  }

  if (FAILED(CoCreateInstance(
          CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) {
    return false;
  }

  // Premultiplied BGRA, which is what Direct2D's B8G8R8A8_UNORM target expects
  // and what the PNG encoder can take straight back.
  if (FAILED(wic->CreateBitmap(
          width, height, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, bitmap.GetAddressOf()))) {
    return false;
  }

  ComPtr<ID2D1Factory> d2d;
  if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf()))) {
    return false;
  }

  // 96 dpi explicitly, so a render is the size the frames say regardless of
  // what the machine taking it is set to. Every coordinate in this project is
  // in React Native's density-independent pixels, and a snapshot that silently
  // came out at 150% on one developer's laptop would compare as a difference
  // against every other host.
  const D2D1_RENDER_TARGET_PROPERTIES properties = D2D1::RenderTargetProperties(
      D2D1_RENDER_TARGET_TYPE_DEFAULT,
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
      96.0f,
      96.0f);

  ComPtr<ID2D1RenderTarget> target;
  if (FAILED(d2d->CreateWicBitmapRenderTarget(bitmap.Get(), properties, target.GetAddressOf()))) {
    return false;
  }

  target->BeginDraw();
  target->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  target->SetTransform(D2D1::Matrix3x2F::Identity());
  // The root paints at its own frame's origin like any other view, and a
  // surface root's origin is (0,0), so nothing here has to compensate.
  root.paint(target.Get());
  return SUCCEEDED(target->EndDraw());
}

} // namespace

RnPixel RnPixels::at(unsigned x, unsigned y) const {
  if (x >= width_ || y >= height_) {
    return {};
  }
  const size_t offset = (static_cast<size_t>(y) * width_ + x) * 4;
  if (offset + 3 >= bgra_.size()) {
    return {};
  }

  const uint8_t blue = bgra_[offset + 0];
  const uint8_t green = bgra_[offset + 1];
  const uint8_t red = bgra_[offset + 2];
  const uint8_t alpha = bgra_[offset + 3];
  if (alpha == 0) {
    return {};
  }

  // Unpremultiplied, so a test compares against the components it set rather
  // than against those components times an alpha it has to work out.
  const auto straight = [alpha](uint8_t component) {
    const int value = (static_cast<int>(component) * 255 + alpha / 2) / alpha;
    return static_cast<uint8_t>(value > 255 ? 255 : value);
  };
  return RnPixel{straight(red), straight(green), straight(blue), alpha};
}

RnPixels renderToPixels(const RnWin32View &root) {
  ComPtr<IWICImagingFactory> wic;
  ComPtr<IWICBitmap> bitmap;
  UINT width = 0;
  UINT height = 0;
  if (!renderToBitmap(root, wic, bitmap, width, height)) {
    return RnPixels{0, 0, {}};
  }

  const UINT stride = width * 4;
  std::vector<uint8_t> buffer(static_cast<size_t>(stride) * height);
  if (FAILED(bitmap->CopyPixels(
          nullptr, stride, static_cast<UINT>(buffer.size()), buffer.data()))) {
    return RnPixels{0, 0, {}};
  }
  return RnPixels{width, height, std::move(buffer)};
}

bool writeSnapshot(const RnWin32View &root, const std::string &path) {
  ComPtr<IWICImagingFactory> wic;
  ComPtr<IWICBitmap> bitmap;
  UINT width = 0;
  UINT height = 0;
  if (!renderToBitmap(root, wic, bitmap, width, height)) {
    return false;
  }

  const std::wstring widePath = widen(path);
  if (widePath.empty()) {
    return false;
  }

  ComPtr<IWICStream> stream;
  if (FAILED(wic->CreateStream(stream.GetAddressOf())) ||
      FAILED(stream->InitializeFromFilename(widePath.c_str(), GENERIC_WRITE))) {
    return false;
  }

  ComPtr<IWICBitmapEncoder> encoder;
  if (FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf())) ||
      FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache))) {
    return false;
  }

  ComPtr<IWICBitmapFrameEncode> frameEncode;
  if (FAILED(encoder->CreateNewFrame(frameEncode.GetAddressOf(), nullptr)) ||
      FAILED(frameEncode->Initialize(nullptr)) || FAILED(frameEncode->SetSize(width, height))) {
    return false;
  }

  // The encoder is allowed to answer with a format it prefers; PNG takes this
  // one, and asking is the documented way rather than an optimisation.
  WICPixelFormatGUID format = GUID_WICPixelFormat32bppPBGRA;
  if (FAILED(frameEncode->SetPixelFormat(&format))) {
    return false;
  }

  if (FAILED(frameEncode->WriteSource(bitmap.Get(), nullptr)) || FAILED(frameEncode->Commit()) ||
      FAILED(encoder->Commit())) {
    return false;
  }

  return true;
}

} // namespace basalt::win32
