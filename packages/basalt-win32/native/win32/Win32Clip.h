// A scoped clip that survives a rotation.
//
// PushAxisAlignedClip is the cheap way to clip and it is wrong wherever a
// transform is in play: it clips to the rectangle's *bounding box* under the
// current transform, so inside a rotated view the clip is a larger upright box
// and content escapes at the corners. A layer with a geometric mask is correct
// under any transform.
//
// Shared because there are two places that clip -- `overflow: hidden` on a view
// and an <Image> that must not paint outside its own frame -- and a platform
// where one of them handles rotation and the other does not is worse than one
// where neither does: the difference only shows up in a picture, on a screen
// nobody is looking at.

#pragma once

#include <windows.h>

#include <d2d1.h>
#include <wrl/client.h>

namespace basalt::win32 {

// A box with its own horizontal and vertical radius at each corner -- what
// React Native resolves every border-radius prop to -- as a geometry.
//
// `radii` is eight numbers in the order describeTree prints them: top-left,
// top-right, bottom-right, bottom-left, each horizontal then vertical. They are
// taken as already clamped so that opposite corners do not overlap, which
// resolveBorderMetrics does before they get here.
//
// D2D1_ROUNDED_RECT has one radius pair for all four corners, which is why
// this host drew a single circular radius for as long as it did. A path with
// an arc per corner is the general case; the two cheaper shapes are kept for
// the boxes that are exactly them, which is nearly every box.
inline Microsoft::WRL::ComPtr<ID2D1Geometry>
roundedBoxGeometry(ID2D1Factory *factory, const D2D1_RECT_F &rect, const float radii[8]) {
  Microsoft::WRL::ComPtr<ID2D1Geometry> result;
  if (factory == nullptr) {
    return result;
  }

  bool any = false;
  bool uniform = true;
  for (int i = 0; i < 8; i++) {
    if (radii[i] > 0.0f) {
      any = true;
    }
    if (radii[i] != radii[0]) {
      uniform = false;
    }
  }

  if (!any) {
    Microsoft::WRL::ComPtr<ID2D1RectangleGeometry> plain;
    if (SUCCEEDED(factory->CreateRectangleGeometry(rect, plain.GetAddressOf()))) {
      result = plain;
    }
    return result;
  }
  if (uniform) {
    Microsoft::WRL::ComPtr<ID2D1RoundedRectangleGeometry> rounded;
    if (SUCCEEDED(factory->CreateRoundedRectangleGeometry(
            D2D1::RoundedRect(rect, radii[0], radii[0]), rounded.GetAddressOf()))) {
      result = rounded;
    }
    return result;
  }

  Microsoft::WRL::ComPtr<ID2D1PathGeometry> path;
  Microsoft::WRL::ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(factory->CreatePathGeometry(path.GetAddressOf())) ||
      FAILED(path->Open(sink.GetAddressOf()))) {
    return result;
  }

  const float left = rect.left;
  const float top = rect.top;
  const float right = rect.right;
  const float bottom = rect.bottom;
  // A corner with either radius zero is square: the line runs on to the
  // corner's end point along the edge, which is the same point.
  const auto corner = [&sink](float x, float y, float rx, float ry) {
    if (rx <= 0.0f || ry <= 0.0f) {
      sink->AddLine(D2D1::Point2F(x, y));
      return;
    }
    sink->AddArc(D2D1::ArcSegment(D2D1::Point2F(x, y),
                                  D2D1::SizeF(rx, ry),
                                  0.0f,
                                  D2D1_SWEEP_DIRECTION_CLOCKWISE,
                                  D2D1_ARC_SIZE_SMALL));
  };

  // Clockwise from the end of the top-left corner.
  sink->BeginFigure(D2D1::Point2F(left + radii[0], top), D2D1_FIGURE_BEGIN_FILLED);
  sink->AddLine(D2D1::Point2F(right - radii[2], top));
  corner(right, top + radii[3], radii[2], radii[3]);
  sink->AddLine(D2D1::Point2F(right, bottom - radii[5]));
  corner(right - radii[4], bottom, radii[4], radii[5]);
  sink->AddLine(D2D1::Point2F(left + radii[6], bottom));
  corner(left, bottom - radii[7], radii[6], radii[7]);
  sink->AddLine(D2D1::Point2F(left, top + radii[1]));
  corner(left + radii[0], top, radii[0], radii[1]);
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  if (SUCCEEDED(sink->Close())) {
    result = path;
  }
  return result;
}

class ScopedGeometryClip {
 public:
  // A null target means "no clip"; the caller then does not have to choose
  // between an if and a scope.
  //
  // One circular radius on every corner: what an <Image> clips to.
  ScopedGeometryClip(ID2D1RenderTarget *target, const D2D1_RECT_F &rect, float radius)
      : target_(target) {
    const float radii[8] = {radius, radius, radius, radius, radius, radius, radius, radius};
    push(rect, radii);
  }

  // Per corner, in roundedBoxGeometry's order: what a view with
  // `overflow: hidden` clips its children to.
  ScopedGeometryClip(ID2D1RenderTarget *target, const D2D1_RECT_F &rect, const float radii[8])
      : target_(target) {
    push(rect, radii);
  }

  ~ScopedGeometryClip() {
    if (pushed_) {
      target_->PopLayer();
    }
  }

  ScopedGeometryClip(const ScopedGeometryClip &) = delete;
  ScopedGeometryClip &operator=(const ScopedGeometryClip &) = delete;

 private:
  void push(const D2D1_RECT_F &rect, const float radii[8]) {
    if (target_ == nullptr) {
      return;
    }

    Microsoft::WRL::ComPtr<ID2D1Factory> factory;
    target_->GetFactory(factory.GetAddressOf());
    const Microsoft::WRL::ComPtr<ID2D1Geometry> mask =
        roundedBoxGeometry(factory.Get(), rect, radii);
    if (!mask) {
      return;
    }

    // One layer object per push. Direct2D pools the backing surfaces itself, so
    // this costs an allocation rather than a render target; caching one per
    // view is the optimisation to make if a profile ever asks for it.
    if (FAILED(target_->CreateLayer(nullptr, layer_.GetAddressOf()))) {
      return;
    }

    auto parameters = D2D1::LayerParameters();
    parameters.contentBounds = D2D1::InfiniteRect();
    parameters.geometricMask = mask.Get();
    target_->PushLayer(parameters, layer_.Get());
    pushed_ = true;
  }

  ID2D1RenderTarget *target_;
  Microsoft::WRL::ComPtr<ID2D1Layer> layer_;
  bool pushed_ = false;
};

} // namespace basalt::win32
