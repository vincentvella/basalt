// The offscreen bitmap an effect's input is drawn into.
//
// Three things in this host need one: `blurRadius` on an <Image>, `filter` on a
// view, and a text shadow. Each draws into a bitmap and runs a Direct2D effect
// over it, and each needs the same thing of that bitmap, which is why asking
// for it lives in one place: **it has to keep its alpha.**
//
// `CreateCompatibleRenderTarget` with no format asked for inherits the parent's.
// In the app the parent is the window's `ID2D1HwndRenderTarget`, and an HWND
// target is opaque -- there is no per-pixel alpha in a window's back buffer. An
// effect over an opaque offscreen is computed against black: a blur fades to
// black rather than to nothing, a drop shadow is cast by a filled rectangle
// rather than by the shape, and a colour matrix over a half-transparent view
// cannot recover the colour, because the colour and the black are already
// mixed. The suite paints into a WIC bitmap that does keep alpha, so leaving it
// inherited is a difference between the test and the app -- the kind that is
// found by somebody looking at a screen rather than by a suite.
//
// So the format is asked for rather than inherited, and asked for in the same
// words everywhere.

#pragma once

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

namespace basalt::win32 {

// An offscreen target `width` by `height`, in the same coordinates as `target`,
// which keeps alpha. Null when there is none to be had, which every caller
// treats as "draw the plain picture instead".
inline Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget>
createOffscreen(ID2D1RenderTarget *target, float width, float height) {
  Microsoft::WRL::ComPtr<ID2D1BitmapRenderTarget> offscreen;
  if (target == nullptr || width <= 0.0f || height <= 0.0f) {
    return offscreen;
  }
  const D2D1_SIZE_F size = D2D1::SizeF(width, height);
  const D2D1_PIXEL_FORMAT format =
      D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED);
  if (FAILED(target->CreateCompatibleRenderTarget(&size,
                                                  nullptr,
                                                  &format,
                                                  D2D1_COMPATIBLE_RENDER_TARGET_OPTIONS_NONE,
                                                  offscreen.GetAddressOf()))) {
    offscreen.Reset();
  }
  return offscreen;
}

} // namespace basalt::win32
