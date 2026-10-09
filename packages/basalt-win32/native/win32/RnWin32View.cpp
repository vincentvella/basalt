#include "RnWin32View.h"

#include "Backface.h"
#include "ControlMetrics.h"
#include "FocusRing.h"
#include "RnWin32Image.h"
#include "RnWin32TextLayout.h"
#include "ScrollIndicator.h"
#include "Win32Clip.h"
#include "Win32Strings.h"

// Before d2d1.h, which wants the base Windows types and does not pull them in
// itself. NOMINMAX and WIN32_LEAN_AND_MEAN come from the package's CMakeLists;
// without the first, windows.h defines `min` and `max` as macros and breaks
// <algorithm> below at a distance.
#include <windows.h>

#include <d2d1.h>
#include <d2d1helper.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <utility>
#include <cmath>
#include <cstdio>

using Microsoft::WRL::ComPtr;

namespace basalt::win32 {

using basalt::kFocusRingAlpha;
using basalt::kFocusRingBlue;
using basalt::kFocusRingGreen;
using basalt::kFocusRingRed;
using basalt::kFocusRingWidth;

namespace {

// A scoped opacity layer, for `opacity` on a view.
//
// A layer rather than multiplying every brush's alpha: React Native's opacity
// composites the subtree as a unit, so two overlapping half-transparent
// children do not show through each other.
class ScopedOpacity {
 public:
  ScopedOpacity(ID2D1RenderTarget *target, float opacity) : target_(target) {
    if (opacity >= 1.0f) {
      return;
    }
    if (FAILED(target_->CreateLayer(nullptr, layer_.GetAddressOf()))) {
      return;
    }
    auto parameters = D2D1::LayerParameters();
    parameters.contentBounds = D2D1::InfiniteRect();
    parameters.opacity = opacity;
    target_->PushLayer(parameters, layer_.Get());
    pushed_ = true;
  }

  ~ScopedOpacity() {
    if (pushed_) {
      target_->PopLayer();
    }
  }

  ScopedOpacity(const ScopedOpacity &) = delete;
  ScopedOpacity &operator=(const ScopedOpacity &) = delete;

 private:
  ID2D1RenderTarget *target_;
  ComPtr<ID2D1Layer> layer_;
  bool pushed_ = false;
};

// Escaped the way g_strescape's output is on the GTK side, so a string with a
// quote or a newline in it stays one line and stays comparable.
void appendEscaped(std::string &out, const std::string &text) {
  for (const char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      default:
        out += c;
        break;
    }
  }
}

template <typename... Args>
void appendFormat(std::string &out, const char *format, Args... args) {
  char buffer[256];
  const int written = std::snprintf(buffer, sizeof(buffer), format, args...);
  if (written > 0) {
    // snprintf returns what it *would* have written, so a truncated field is
    // clamped here rather than read past the buffer.
    const int limit = static_cast<int>(sizeof(buffer)) - 1;
    out.append(buffer, static_cast<size_t>(written < limit ? written : limit));
  }
}

unsigned toByte(float component) {
  return static_cast<unsigned>(component * 255.0f + 0.5f);
}

// Two 2D affine matrices, in Direct2D's Matrix3x2F order and its row-vector
// convention: `compose(a, b)` is "apply a, then b", which is what
// `Matrix3x2F::SetProduct(a, b)` computes. Written out in floats rather than
// built with D2D1::Matrix3x2F so that hit testing -- which wants none of
// Direct2D -- can use the same composition painting does.
void compose(const float a[6], const float b[6], float out[6]) {
  const float r[6] = {
      a[0] * b[0] + a[1] * b[2],
      a[0] * b[1] + a[1] * b[3],
      a[2] * b[0] + a[3] * b[2],
      a[2] * b[1] + a[3] * b[3],
      a[4] * b[0] + a[5] * b[2] + b[4],
      a[4] * b[1] + a[5] * b[3] + b[5],
  };
  for (int i = 0; i < 6; i++) {
    out[i] = r[i];
  }
}

// Maps a point through the inverse of `m`. False when `m` is singular, which is
// a view scaled to nothing: it paints no pixels, so nothing can be over it.
bool invertPoint(const float m[6], float x, float y, float &outX, float &outY) {
  const float determinant = m[0] * m[3] - m[1] * m[2];
  if (std::fabs(determinant) < 1e-6f) {
    return false;
  }
  const float shiftedX = x - m[4];
  const float shiftedY = y - m[5];
  outX = (shiftedX * m[3] - shiftedY * m[2]) / determinant;
  outY = (shiftedY * m[0] - shiftedX * m[1]) / determinant;
  return true;
}

} // namespace

RnWin32View::RnWin32View(int32_t tag) : tag_(tag) {}

RnWin32View::~RnWin32View() {
  // A view owns neither its children nor its parent; the registry owns every
  // view. What a destructor does owe is that nothing is left pointing at it.
  if (parent_ != nullptr) {
    parent_->removeChild(this);
  }
  for (RnWin32View *child : children_) {
    child->parent_ = nullptr;
  }
}

// --- Geometry --------------------------------------------------------------

void RnWin32View::setFrame(float x, float y, float width, float height) {
  frame_ = RnRect{x, y, width, height};
}

void RnWin32View::setScrollOffset(float x, float y) {
  scrollX_ = x;
  scrollY_ = y;
}

void RnWin32View::setScrollIndicators(float verticalOffset,
                                      float verticalLength,
                                      float horizontalOffset,
                                      float horizontalLength) {
  indicatorVerticalOffset_ = verticalOffset;
  indicatorVerticalLength_ = verticalLength;
  indicatorHorizontalOffset_ = horizontalOffset;
  indicatorHorizontalLength_ = horizontalLength;
}

// --- Appearance ------------------------------------------------------------

void RnWin32View::setBackgroundColor(float red,
                                     float green,
                                     float blue,
                                     float alpha,
                                     bool hasColor) {
  hasBackgroundColor_ = hasColor;
  backgroundColor_[0] = red;
  backgroundColor_[1] = green;
  backgroundColor_[2] = blue;
  backgroundColor_[3] = alpha;
}

void RnWin32View::setOpacity(float opacity) {
  opacity_ = opacity;
}

void RnWin32View::setTransform(const float *matrix16) {
  if (matrix16 == nullptr) {
    hasTransform_ = false;
    transform_[0] = 1.0f;
    transform_[1] = 0.0f;
    transform_[2] = 0.0f;
    transform_[3] = 1.0f;
    transform_[4] = 0.0f;
    transform_[5] = 0.0f;
    updateBackFace();
    return;
  }

  // CSS matrix3d order, which is column-major: m11 m12 m13 m14 m21 ... So the
  // 2D affine part is elements 0, 1, 4, 5, 12 and 13, and those are the same
  // six the GTK side pulls out of its graphene matrix and the macOS side out of
  // its CATransform3D.
  hasTransform_ = true;
  transform_[0] = matrix16[0];
  transform_[1] = matrix16[1];
  transform_[2] = matrix16[4];
  transform_[3] = matrix16[5];
  transform_[4] = matrix16[12];
  transform_[5] = matrix16[13];
  updateBackFace();
}

void RnWin32View::setHidesBackFace(bool hides) {
  if (hidesBackFace_ == hides) {
    return;
  }
  hidesBackFace_ = hides;
  updateBackFace();
}

// Hidden the same way the app's own `display: none` hides a view, because the
// effect is the same -- not painted, not hit tested, children included.
//
// The two reasons are kept apart and combined here. `hidden_` is one flag and
// `display: none` writes it too, so whichever of the two spoke last used to
// answer for both -- and layout metrics are applied after props, so a card
// turned away from the viewer was reliably un-hidden a moment later.
void RnWin32View::applyVisibility() {
  hidden_ = hiddenByApp_ || hiddenByBackFace_;
}

void RnWin32View::updateBackFace() {
  // Recomputed rather than returned early on, so that turning the prop off
  // shows a view it had hidden instead of leaving it hidden forever.
  hiddenByBackFace_ =
      hidesBackFace_ &&
      basalt::facesAway(transform_[0], transform_[1], transform_[2], transform_[3]);
  applyVisibility();
}

void RnWin32View::setClipsChildren(bool clips) {
  clipsChildren_ = clips;
}

void RnWin32View::setCornerRadius(float radius) {
  const float radii[8] = {radius, radius, radius, radius, radius, radius, radius, radius};
  setCornerRadii(radii);
}

void RnWin32View::setCornerRadii(const float radii[8]) {
  hasCornerRadii_ = false;
  for (int i = 0; i < 8; i++) {
    cornerRadii_[i] = radii != nullptr ? radii[i] : 0.0f;
    if (cornerRadii_[i] > 0.0f) {
      hasCornerRadii_ = true;
    }
  }
}

void RnWin32View::setBorders(const float widths[4], const float colours[16]) {
  // "Has a border" is decided here, once, the way rn_view_set_borders decides
  // it on GTK: some edge both wide and visible. Paint and the tree dump both
  // read this flag, so they cannot disagree about whether a border exists.
  hasBorders_ = false;
  for (int i = 0; i < 4; i++) {
    borderWidths_[i] = widths != nullptr ? widths[i] : 0.0f;
    for (int c = 0; c < 4; c++) {
      borderColours_[i * 4 + c] = colours != nullptr ? colours[i * 4 + c] : 0.0f;
    }
    if (borderWidths_[i] > 0.0f && borderColours_[i * 4 + 3] > 0.0f) {
      hasBorders_ = true;
    }
  }
}

void RnWin32View::setZIndex(int zIndex) {
  zIndex_ = zIndex;
}

void RnWin32View::setHidden(bool hidden) {
  hiddenByApp_ = hidden;
  applyVisibility();
}

// --- Content ----------------------------------------------------------------

void RnWin32View::setTextLayout(std::shared_ptr<RnWin32TextLayout> layout) {
  textLayout_ = std::move(layout);
}

void RnWin32View::setImageTint(bool hasTint, const float components[4]) {
  // No repaint request here, for the same reason `setImage` makes none: this
  // host invalidates once per mounted transaction rather than per view. See
  // requestRepaint in main_win32.cpp.
  hasImageTint_ = hasTint;
  if (hasTint && components != nullptr) {
    for (int i = 0; i < 4; i++) {
      imageTint_[i] = components[i];
    }
  }
}

void RnWin32View::setImage(std::shared_ptr<RnWin32Image> image, RnImageFit fit) {
  image_ = std::move(image);
  imageFit_ = fit;
}

void RnWin32View::setPainter(std::shared_ptr<RnWin32Painter> painter) {
  painter_ = std::move(painter);
}

// --- Accessibility ----------------------------------------------------------

void RnWin32View::setAccessibleInfo(const RnAccessibleInfo &info) {
  accessible_ = info;
}

IRawElementProviderSimple *RnWin32View::createAccessibleProvider() const {
  return basalt::win32::createAccessibleProvider(accessible_);
}

// --- Geometry, resolved -----------------------------------------------------

void RnWin32View::localToParent(float out[6]) const {
  const float translation[6] = {1.0f, 0.0f, 0.0f, 1.0f, frame_.x, frame_.y};
  if (!hasTransform_) {
    for (int i = 0; i < 6; i++) {
      out[i] = translation[i];
    }
    return;
  }

  // Anchored at the view's centre by moving the centre to the origin and back
  // around the transform, which is what React Native means by an untouched
  // `transformOrigin` and what every other platform here does. The frame's
  // translation comes last because this composes "apply a, then b".
  const float centreX = frame_.width / 2.0f;
  const float centreY = frame_.height / 2.0f;
  const float toOrigin[6] = {1.0f, 0.0f, 0.0f, 1.0f, -centreX, -centreY};
  const float fromOrigin[6] = {1.0f, 0.0f, 0.0f, 1.0f, centreX, centreY};

  float composed[6];
  compose(toOrigin, transform_, composed);
  compose(composed, fromOrigin, composed);
  compose(composed, translation, out);
}

bool RnWin32View::pageToLocal(const RnWin32View *root,
                              float pageX,
                              float pageY,
                              float &outX,
                              float &outY) const {
  float toPage[6] = {1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f};

  for (const RnWin32View *node = this; node != nullptr && node != root;) {
    float local[6];
    node->localToParent(local);
    compose(toPage, local, toPage);

    const RnWin32View *parent = node->parent();
    if (parent == nullptr) {
      break;
    }
    // A scrolled ancestor moves its children by its offset, which is not part
    // of any child's own placement -- the same offset `paintChildren`
    // subtracts and `hitTest` adds back.
    const float scroll[6] = {1.0f, 0.0f, 0.0f, 1.0f, -parent->scrollX(), -parent->scrollY()};
    compose(toPage, scroll, toPage);
    node = parent;
  }

  return invertPoint(toPage, pageX, pageY, outX, outY);
}

std::vector<RnWin32View *> RnWin32View::childrenInPaintOrder() const {
  std::vector<RnWin32View *> ordered = children_;
  const bool needsSorting =
      std::any_of(ordered.begin(), ordered.end(), [](const RnWin32View *child) {
        return child->zIndex() != 0;
      });
  if (needsSorting) {
    // Stable, so equal zIndex keeps document order -- which is what CSS and
    // React Native both promise.
    std::stable_sort(
        ordered.begin(), ordered.end(), [](const RnWin32View *a, const RnWin32View *b) {
          return a->zIndex() < b->zIndex();
        });
  }
  return ordered;
}

// --- Tree ------------------------------------------------------------------

void RnWin32View::insertChild(RnWin32View *child, int index) {
  if (child == nullptr || child == this) {
    return;
  }
  if (child->parent_ != nullptr) {
    child->parent_->removeChild(child);
  }

  // Fabric's index counts positions in the parent's *final* child list, so a
  // later mutation lands between two existing children. Clamped rather than
  // trusted: a transaction racing a surface teardown can name a position that
  // no longer exists, and that has to be survivable.
  const int count = static_cast<int>(children_.size());
  const int at = std::clamp(index, 0, count);
  children_.insert(children_.begin() + at, child);
  child->parent_ = this;
}

void RnWin32View::removeChild(RnWin32View *child) {
  const auto it = std::find(children_.begin(), children_.end(), child);
  if (it == children_.end()) {
    return;
  }
  (*it)->parent_ = nullptr;
  children_.erase(it);
}

// --- Painting --------------------------------------------------------------

void RnWin32View::paint(ID2D1RenderTarget *target) const {
  if (target == nullptr || hidden_) {
    return;
  }

  D2D1::Matrix3x2F parentTransform;
  target->GetTransform(&parentTransform);

  // From localToParent rather than composed here, so that hit testing -- which
  // inverts the same six numbers -- cannot end up with a different idea of
  // where this view is.
  float localValues[6];
  localToParent(localValues);
  const D2D1::Matrix3x2F local(localValues[0],
                               localValues[1],
                               localValues[2],
                               localValues[3],
                               localValues[4],
                               localValues[5]);
  target->SetTransform(local * parentTransform);

  const D2D1_RECT_F bounds = D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height);

  {
    const ScopedOpacity fade(target, opacity_);

    // The background is always clipped to the rounded box, even when children
    // are not: `overflow: visible` lets a child escape the corner, but the
    // view's own fill still has to respect its border radius.
    if (hasBackgroundColor_) {
      ComPtr<ID2D1SolidColorBrush> brush;
      // An explicit sRGB colour. Letting the target pick a device space shifts
      // every colour slightly on a wide-gamut display -- not wrong exactly, but
      // different from the same app on Linux, which is the one thing this
      // project is trying not to be.
      const D2D1_COLOR_F colour = D2D1::ColorF(
          backgroundColor_[0], backgroundColor_[1], backgroundColor_[2], backgroundColor_[3]);
      if (SUCCEEDED(target->CreateSolidColorBrush(colour, brush.GetAddressOf()))) {
        if (hasCornerRadii_) {
          // The real shape: each corner its own, elliptical where React Native
          // says so. See roundedBoxGeometry in Win32Clip.h.
          ComPtr<ID2D1Factory> factory;
          target->GetFactory(factory.GetAddressOf());
          const ComPtr<ID2D1Geometry> box = roundedBoxGeometry(factory.Get(), bounds, cornerRadii_);
          if (box) {
            target->FillGeometry(box.Get(), brush.Get());
          }
        } else {
          target->FillRectangle(bounds, brush.Get());
        }
      }
    }

    // Then the image, then the text, then the children -- the order
    // `rn_view_snapshot` uses on GTK. Nothing in React Native puts two of these
    // on one view, but the order still has to be decided somewhere, and it is
    // cheaper to match than to argue about later.
    if (image_ != nullptr) {
      // The four floats as stored; RnWin32Image builds the Direct2D colour,
      // where d2d1.h is in scope. See its header for why it takes floats.
      image_->draw(target,
                   frame_.width,
                   frame_.height,
                   imageFit_,
                   hasImageTint_ ? imageTint_ : nullptr);
    }

    // Then anything this layer cannot draw itself -- the Skia surface behind a
    // `<Canvas>`, and nothing else so far. Here rather than in its own pass so
    // that it is inside the clip, the transform and the opacity this view has
    // already applied, which is the whole point of drawing it in the walk.
    if (painter_ != nullptr) {
      painter_->draw(target, frame_.width, frame_.height);
    }

    // Text sits above the background and below any children, which is the
    // order `<Text>` with nested views expects. The paragraph draws itself at
    // the view's own origin, in the box Yoga gave the view -- the same box it
    // was measured against, because both go through RnWin32TextLayout.
    if (textLayout_ != nullptr) {
      textLayout_->draw(target, frame_.width, frame_.height);
    }

    // A control sits where the text would: above the background, below the
    // children. Nothing in React Native puts children inside a <Switch>, but
    // the order still has to be decided somewhere.
    if (control_ != Control::None) {
      paintControl(target);
    }

    paintChildren(target);

    // paintChildren leaves a ScrollView's offset applied, and nothing below
    // here scrolls with the content -- so put the transform back to this view's
    // own before drawing any of it.
    target->SetTransform(local * parentTransform);

    // The scrollbars: an overlay, so above the content and above any children,
    // which is what "overlay indicator" means -- and below the DevTools overlay
    // and the focus ring, neither of which belongs to the app.
    if (indicatorVerticalLength_ > 0.0f || indicatorHorizontalLength_ > 0.0f) {
      paintScrollIndicators(target);
    }

    // The border: over the content, as GTK and AppKit draw it, and inside the
    // view's own rounded box rather than around it -- React Native's border is
    // part of the box, which is why Yoga has already inset the content by it.
    // Below the DevTools overlay and the focus ring, which are not the app's.
    if (hasBorders_) {
      paintBorders(target);
    }

    // The outline, outside the box and over the border: it is not a border, and
    // it sits `offset` away from the box's edge. After the children for the
    // clipping reason in paintOutline, and below the DevTools overlay and the
    // focus ring, which are not the app's.
    paintOutline(target);

    // React DevTools' overlay, over everything including the children. Above
    // the app on purpose: it is not part of it, and an inspected element half
    // hidden behind a card would be pointing at the wrong thing.
    if (!highlights_.empty()) {
      paintHighlights(target);
    }

    // The focus ring, over everything including the children, because it is the
    // answer to "where am I" and must not be hidden by what it is drawn on.
    //
    // Inside the view's own bounds rather than around them, which is where both
    // other hosts draw it -- AppKit's is clipped to the view and GTK's matches
    // that on purpose. Stroked down the middle of the line, so the rectangle is
    // inset by half of it to keep the whole ring inside.
    if (showsFocusRing_) {
      ComPtr<ID2D1SolidColorBrush> brush;
      const D2D1_COLOR_F colour = D2D1::ColorF(
          kFocusRingRed, kFocusRingGreen, kFocusRingBlue, kFocusRingAlpha);
      if (SUCCEEDED(target->CreateSolidColorBrush(colour, brush.GetAddressOf()))) {
        const float inset = kFocusRingWidth / 2.0f;
        const D2D1_RECT_F ring = D2D1::RectF(inset,
                                             inset,
                                             (std::max)(inset, frame_.width - inset),
                                             (std::max)(inset, frame_.height - inset));
        if (hasCornerRadii_) {
          // The view's own shape, each radius pulled in by the inset so the
          // ring follows the corner rather than cutting across it.
          float insetRadii[8];
          for (int i = 0; i < 8; i++) {
            insetRadii[i] = (std::max)(0.0f, cornerRadii_[i] - inset);
          }
          ComPtr<ID2D1Factory> factory;
          target->GetFactory(factory.GetAddressOf());
          const ComPtr<ID2D1Geometry> outline =
              roundedBoxGeometry(factory.Get(), ring, insetRadii);
          if (outline) {
            target->DrawGeometry(outline.Get(), brush.Get(), kFocusRingWidth);
          }
        } else {
          target->DrawRectangle(ring, brush.Get(), kFocusRingWidth);
        }
      }
    }
  }

  target->SetTransform(parentTransform);
}

// The border, as the ring between the view's rounded box and that box inset by
// each edge's width.
//
// The inner corners follow CSS: each outer radius less the width of the edge
// it meets, and never below zero -- which is why a thick border on a small
// radius has a square inside, on every platform.
//
// One colour all round is one fill. Different colours per edge are how CSS
// draws them: the ring is split along the diagonals from each outer corner to
// the matching inner one, and each side is filled in its own colour. Those
// sides are filled aliased inside a layer whose mask is the anti-aliased ring:
// adjacent anti-aliased fills leave a faint seam down each diagonal where their
// coverages meet, and the mask already gives the ring's own edges their
// smoothing.
// The dash pattern for a style, in Direct2D's units.
//
// Direct2D counts dash lengths in multiples of the stroke width, where GSK and
// Core Animation take absolute ones -- so the other two hosts' {3w, 2w} is
// {3, 2} here, and theirs is scaled to the width for the reason a browser
// scales it: a fixed pattern reads as a hairline on a thick border and as a
// solid line on a thin one. Dotted is a zero-length dash with round caps,
// which is what makes a dot a dot rather than a short dash.
//
// Answers with the stroke style, or null for a solid line and for a failure,
// which draws solid: a ring drawn solid is wrong in a way a reader can see, and
// no ring at all looks like the prop being ignored.
static ComPtr<ID2D1StrokeStyle> dashStyleFor(ID2D1Factory *factory,
                                             RnWin32View::LineStyle style) {
  if (factory == nullptr || style == RnWin32View::LineStyle::Solid) {
    return nullptr;
  }
  const bool dots = style == RnWin32View::LineStyle::Dotted;
  const D2D1_CAP_STYLE cap = dots ? D2D1_CAP_STYLE_ROUND : D2D1_CAP_STYLE_FLAT;
  const float pattern[2] = {dots ? 0.0f : 3.0f, 2.0f};
  const D2D1_STROKE_STYLE_PROPERTIES properties = D2D1::StrokeStyleProperties(
      cap, cap, cap, D2D1_LINE_JOIN_MITER, 10.0f, D2D1_DASH_STYLE_CUSTOM, 0.0f);

  ComPtr<ID2D1StrokeStyle> stroke;
  if (FAILED(factory->CreateStrokeStyle(properties, pattern, 2, stroke.GetAddressOf()))) {
    return nullptr;
  }
  return stroke;
}

void RnWin32View::setBorderStyle(LineStyle style) {
  borderStyle_ = style;
}

void RnWin32View::setOutline(float width,
                             float offset,
                             const float colour[4],
                             LineStyle style) {
  outlineWidth_ = width > 0.0f ? width : 0.0f;
  outlineOffset_ = offset;
  for (int component = 0; component < 4; component++) {
    outlineColour_[component] = colour != nullptr ? colour[component] : 0.0f;
  }
  outlineStyle_ = style;
}

// CSS's outline, outside the box and over everything.
//
// **Nothing clips it, and that is the decision rather than an oversight.** CSS
// does not clip an element's own outline for `overflow: hidden`, so a ring
// drawn inside the clip would vanish on exactly the views that most often carry
// one. Here that is free: `paintChildren` scopes its own clip, so by the time
// this runs the clip is already popped. GTK gets it the same way, by appending
// after the children; AppKit has to move the ring into the parent's layer.
//
// Stroked rather than filled like the border, for the two reasons
// backlog/correctness.md records: a stroke takes a dash pattern, so dotted and
// dashed need no second mechanism, and it takes an arbitrary path, so
// elliptical radii need no special case. A stroke straddles its path, so the
// path is the ring's centre line: offset plus half the width out from the
// border edge, which is where the other two hosts put theirs.
void RnWin32View::paintOutline(ID2D1RenderTarget *target) const {
  if (outlineWidth_ <= 0.0f || outlineColour_[3] <= 0.0f) {
    return;
  }

  ComPtr<ID2D1Factory> factory;
  target->GetFactory(factory.GetAddressOf());
  if (!factory) {
    return;
  }

  // The ring's outer edge is the box grown by the offset plus the width; its
  // centre line is half a width back from that.
  const float centre = outlineOffset_ + outlineWidth_ / 2.0f;
  const D2D1_RECT_F rect = D2D1::RectF(
      -centre, -centre, frame_.width + centre, frame_.height + centre);

  // Each non-zero radius grows by what the ring moved out, so the ring stays
  // concentric with a rounded card; a corner that was square stays square,
  // which is what React Native's iOS half does and what both other hosts do.
  float radii[8];
  for (int index = 0; index < 8; index++) {
    radii[index] = cornerRadii_[index] > 0.0f ? cornerRadii_[index] + centre : 0.0f;
  }

  const ComPtr<ID2D1Geometry> ring = roundedBoxGeometry(factory.Get(), rect, radii);
  if (!ring) {
    return;
  }

  ComPtr<ID2D1SolidColorBrush> brush;
  const D2D1_COLOR_F colour = D2D1::ColorF(
      outlineColour_[0], outlineColour_[1], outlineColour_[2], outlineColour_[3]);
  if (FAILED(target->CreateSolidColorBrush(colour, brush.GetAddressOf()))) {
    return;
  }

  const ComPtr<ID2D1StrokeStyle> dashed = dashStyleFor(factory.Get(), outlineStyle_);
  target->DrawGeometry(ring.Get(), brush.Get(), outlineWidth_, dashed.Get());
}

// A dotted or dashed border: one stroked path around the rounded box rather
// than four filled edges.
//
// Which is why the style belongs to the whole border and not to a side, the
// same decision both other hosts made: a stroked path carries one dash pattern.
// The width and the colour are the first side's, for the same reason.
//
// Inset by half the width, because a stroke straddles its path where the filled
// ring sits inside the box. Without that a 4pt dashed border would paint two
// points outside the view and overlap its neighbour.
void RnWin32View::paintStrokedBorder(ID2D1RenderTarget *target) const {
  ComPtr<ID2D1Factory> factory;
  target->GetFactory(factory.GetAddressOf());
  if (!factory) {
    return;
  }

  const float width = borderWidths_[0] > 0.0f ? borderWidths_[0] : 1.0f;
  const D2D1_RECT_F rect = D2D1::RectF(width / 2.0f,
                                       width / 2.0f,
                                       (std::max)(width / 2.0f, frame_.width - width / 2.0f),
                                       (std::max)(width / 2.0f, frame_.height - width / 2.0f));
  // The radii shrink with the path, so a dashed border on a rounded card keeps
  // the card's corner rather than cutting across it.
  float radii[8];
  for (int index = 0; index < 8; index++) {
    radii[index] = cornerRadii_[index] > width / 2.0f ? cornerRadii_[index] - width / 2.0f : 0.0f;
  }

  const ComPtr<ID2D1Geometry> path = roundedBoxGeometry(factory.Get(), rect, radii);
  if (!path) {
    return;
  }

  ComPtr<ID2D1SolidColorBrush> brush;
  const D2D1_COLOR_F colour = D2D1::ColorF(
      borderColours_[0], borderColours_[1], borderColours_[2], borderColours_[3]);
  if (FAILED(target->CreateSolidColorBrush(colour, brush.GetAddressOf()))) {
    return;
  }

  const ComPtr<ID2D1StrokeStyle> dashed = dashStyleFor(factory.Get(), borderStyle_);
  target->DrawGeometry(path.Get(), brush.Get(), width, dashed.Get());
}

void RnWin32View::paintBorders(ID2D1RenderTarget *target) const {
  // Dotted and dashed are a different drawing entirely; see paintStrokedBorder.
  if (borderStyle_ != LineStyle::Solid) {
    paintStrokedBorder(target);
    return;
  }

  ComPtr<ID2D1Factory> factory;
  target->GetFactory(factory.GetAddressOf());
  if (!factory) {
    return;
  }

  const float width = frame_.width;
  const float height = frame_.height;
  const float top = borderWidths_[0];
  const float right = borderWidths_[1];
  const float bottom = borderWidths_[2];
  const float left = borderWidths_[3];

  const auto floor0 = [](float value) { return (std::max)(0.0f, value); };
  const D2D1_RECT_F outerRect = D2D1::RectF(0.0f, 0.0f, width, height);
  const D2D1_RECT_F innerRect = D2D1::RectF(
      left, top, (std::max)(left, width - right), (std::max)(top, height - bottom));
  const float innerRadii[8] = {
      floor0(cornerRadii_[0] - left),
      floor0(cornerRadii_[1] - top),
      floor0(cornerRadii_[2] - right),
      floor0(cornerRadii_[3] - top),
      floor0(cornerRadii_[4] - right),
      floor0(cornerRadii_[5] - bottom),
      floor0(cornerRadii_[6] - left),
      floor0(cornerRadii_[7] - bottom),
  };

  const ComPtr<ID2D1Geometry> outer = roundedBoxGeometry(factory.Get(), outerRect, cornerRadii_);
  const ComPtr<ID2D1Geometry> inner = roundedBoxGeometry(factory.Get(), innerRect, innerRadii);
  if (!outer || !inner) {
    return;
  }

  ComPtr<ID2D1PathGeometry> ring;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(factory->CreatePathGeometry(ring.GetAddressOf())) ||
      FAILED(ring->Open(sink.GetAddressOf())) ||
      FAILED(outer->CombineWithGeometry(
          inner.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get())) ||
      FAILED(sink->Close())) {
    return;
  }

  const auto colourOf = [this](int edge) {
    const float *c = &borderColours_[edge * 4];
    return D2D1::ColorF(c[0], c[1], c[2], c[3]);
  };

  bool oneColour = true;
  for (int edge = 1; edge < 4; edge++) {
    for (int c = 0; c < 4; c++) {
      if (borderColours_[edge * 4 + c] != borderColours_[c]) {
        oneColour = false;
      }
    }
  }

  if (oneColour) {
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(target->CreateSolidColorBrush(colourOf(0), brush.GetAddressOf()))) {
      target->FillGeometry(ring.Get(), brush.Get());
    }
    return;
  }

  ComPtr<ID2D1Layer> layer;
  if (FAILED(target->CreateLayer(nullptr, layer.GetAddressOf()))) {
    return;
  }
  auto parameters = D2D1::LayerParameters();
  parameters.contentBounds = D2D1::InfiniteRect();
  parameters.geometricMask = ring.Get();
  target->PushLayer(parameters, layer.Get());

  const D2D1_ANTIALIAS_MODE previous = target->GetAntialiasMode();
  target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);

  // Each side, from its two outer corners to the matching inner ones. Top,
  // right, bottom, left, as the widths and colours are kept.
  const D2D1_POINT_2F sides[4][4] = {
      {{0.0f, 0.0f}, {width, 0.0f}, {width - right, top}, {left, top}},
      {{width, 0.0f}, {width, height}, {width - right, height - bottom}, {width - right, top}},
      {{width, height}, {0.0f, height}, {left, height - bottom}, {width - right, height - bottom}},
      {{0.0f, height}, {0.0f, 0.0f}, {left, top}, {left, height - bottom}},
  };
  for (int edge = 0; edge < 4; edge++) {
    if (borderWidths_[edge] <= 0.0f || borderColours_[edge * 4 + 3] <= 0.0f) {
      continue;
    }
    ComPtr<ID2D1PathGeometry> side;
    ComPtr<ID2D1GeometrySink> sideSink;
    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(factory->CreatePathGeometry(side.GetAddressOf())) ||
        FAILED(side->Open(sideSink.GetAddressOf())) ||
        FAILED(target->CreateSolidColorBrush(colourOf(edge), brush.GetAddressOf()))) {
      continue;
    }
    sideSink->BeginFigure(sides[edge][0], D2D1_FIGURE_BEGIN_FILLED);
    sideSink->AddLines(&sides[edge][1], 3);
    sideSink->EndFigure(D2D1_FIGURE_END_CLOSED);
    if (SUCCEEDED(sideSink->Close())) {
      target->FillGeometry(side.Get(), brush.Get());
    }
  }

  target->SetAntialiasMode(previous);
  target->PopLayer();
}

// Reads the current transform rather than being handed one, which is what keeps
// D2D1_MATRIX_3X2_F -- and windows.h behind it -- out of the header.

// ---------------------------------------------------------------------------
// Controls
// ---------------------------------------------------------------------------

namespace {

// Where a spinner is in its turn, 0..1, from a monotonic clock.
//
// A clock rather than per-view state: every spinner on screen turns together,
// which is what a real toolkit spinner does, and it means a view needs no
// animation bookkeeping of its own. One turn a second, which is roughly what
// GtkSpinner and NSProgressIndicator do.
//
// Here rather than in core/DesktopControls.h because this target deliberately
// does not link React Native, and that header does.
float spinnerTurn() {
  using namespace std::chrono;
  const double seconds = duration<double>(steady_clock::now().time_since_epoch()).count();
  return static_cast<float>(seconds - std::floor(seconds));
}

D2D1_COLOR_F colourOr(const float rgba[4], bool has, D2D1_COLOR_F fallback) {
  return has ? D2D1::ColorF(rgba[0], rgba[1], rgba[2], rgba[3]) : fallback;
}

// The greys the rest of this host draws controls in. Windows' own accent colour
// would be more native and is a per-user setting read through UISettings, which
// is a WinRT dependency this package does not have; React Native's own default
// switch is not accent-coloured either.
constexpr float kTrackOffGrey = 0.78f;
constexpr float kTrackOnBlue[3] = {0.20f, 0.60f, 0.35f};
constexpr float kSpinnerGrey = 0.45f;

} // namespace

void RnWin32View::setControl(Control kind, const ControlStyle &style, std::string description) {
  control_ = kind;
  controlStyle_ = style;
  controlDescription_ = std::move(description);
}

bool RnWin32View::hasAnimatingSpinner() const {
  if (control_ == Control::Spinner && controlStyle_.on) {
    return true;
  }
  if (hidden_) {
    return false;
  }
  for (const RnWin32View *child : children_) {
    if (child != nullptr && child->hasAnimatingSpinner()) {
      return true;
    }
  }
  return false;
}

void RnWin32View::setHighlights(std::vector<Highlight> highlights) {
  highlights_ = std::move(highlights);
}

void RnWin32View::paintHighlights(ID2D1RenderTarget *target) const {
  ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0, 0, 0, 1.0f), brush.GetAddressOf()))) {
    return;
  }
  for (const Highlight &highlight : highlights_) {
    const D2D1_RECT_F area = D2D1::RectF(highlight.x,
                                         highlight.y,
                                         highlight.x + highlight.width,
                                         highlight.y + highlight.height);
    if (highlight.filled) {
      brush->SetColor(
          D2D1::ColorF(highlight.color[0], highlight.color[1], highlight.color[2]));
      brush->SetOpacity(highlight.color[3]);
      target->FillRectangle(area, brush.Get());
    }
    // Opaque on the outline even when the fill is not, so the edge of an
    // inspected element is a line rather than a suggestion. Stroked down the
    // middle, so the rectangle is inset by half the width to keep it inside.
    brush->SetColor(D2D1::ColorF(highlight.color[0], highlight.color[1], highlight.color[2]));
    brush->SetOpacity(1.0f);
    const float inset = basalt::kHighlightBorderWidth / 2.0f;
    target->DrawRectangle(
        D2D1::RectF(area.left + inset, area.top + inset, area.right - inset, area.bottom - inset),
        brush.Get(),
        basalt::kHighlightBorderWidth);
  }
}

void RnWin32View::paintControl(ID2D1RenderTarget *target) const {
  if (control_ == Control::Spinner) {
    // A stopped indicator draws nothing at all, which is what
    // `hidesWhenStopped` means and what a <RefreshControl> that is not
    // refreshing does.
    if (!controlStyle_.on && controlStyle_.hidesWhenStopped) {
      return;
    }

    // Eight dots around a circle, the leading one opaque and the rest fading
    // behind it. The same shape GtkSpinner and NSProgressIndicator draw, which
    // matters because these three hosts are screenshotted side by side.
    const float diameter = std::min(
        {controlStyle_.large ? basalt::kSpinnerLarge : basalt::kSpinnerSmall,
         frame_.width,
         frame_.height});
    if (diameter <= 0.0f) {
      return;
    }
    const float centreX = frame_.width / 2.0f;
    const float centreY = frame_.height / 2.0f;
    const float radius = diameter / 2.0f;
    const float dotRadius = std::max(radius / 6.0f, 1.0f);

    ComPtr<ID2D1SolidColorBrush> brush;
    const D2D1_COLOR_F base = colourOr(controlStyle_.thumb,
                                       controlStyle_.hasThumb,
                                       D2D1::ColorF(kSpinnerGrey, kSpinnerGrey, kSpinnerGrey, 1.0f));
    if (FAILED(target->CreateSolidColorBrush(base, brush.GetAddressOf()))) {
      return;
    }

    constexpr int kDots = 8;
    // A stopped indicator that is still shown is drawn still, at phase zero,
    // rather than at whatever the clock happens to say -- so a screenshot of a
    // stopped spinner is the same picture every time.
    const float turn = controlStyle_.on ? spinnerTurn() : 0.0f;
    const int leading = static_cast<int>(turn * kDots) % kDots;
    for (int i = 0; i < kDots; i++) {
      const float angle = 6.2831853f * static_cast<float>(i) / static_cast<float>(kDots);
      const int behind = (leading - i + kDots) % kDots;
      brush->SetOpacity(1.0f - static_cast<float>(behind) / static_cast<float>(kDots));
      const D2D1_ELLIPSE dot = D2D1::Ellipse(
          D2D1::Point2F(centreX + std::cos(angle) * (radius - dotRadius),
                        centreY + std::sin(angle) * (radius - dotRadius)),
          dotRadius,
          dotRadius);
      target->FillEllipse(dot, brush.Get());
    }
    return;
  }

  // A switch: a rounded track with a circular thumb at one end. Sized from
  // core/ControlMetrics.h -- the same numbers the shadow node measures to --
  // and centred, so a <Switch> given a bigger box keeps its shape rather than
  // stretching.
  const float width = std::min(basalt::kSwitchWidth, frame_.width);
  const float height = std::min(basalt::kSwitchHeight, frame_.height);
  if (width <= 0.0f || height <= 0.0f) {
    return;
  }
  const float left = (frame_.width - width) / 2.0f;
  const float top = (frame_.height - height) / 2.0f;

  const D2D1_COLOR_F trackColour = controlStyle_.on
      ? colourOr(controlStyle_.trackOn,
                 controlStyle_.hasTrackOn,
                 D2D1::ColorF(kTrackOnBlue[0], kTrackOnBlue[1], kTrackOnBlue[2], 1.0f))
      : colourOr(controlStyle_.trackOff,
                 controlStyle_.hasTrackOff,
                 D2D1::ColorF(kTrackOffGrey, kTrackOffGrey, kTrackOffGrey, 1.0f));

  ComPtr<ID2D1SolidColorBrush> brush;
  if (FAILED(target->CreateSolidColorBrush(trackColour, brush.GetAddressOf()))) {
    return;
  }
  // Half opacity for a disabled switch, which is what every platform does and
  // is the only thing `disabled` can mean to something that draws itself.
  brush->SetOpacity(controlStyle_.disabled ? 0.4f : 1.0f);
  const float radius = height / 2.0f;
  target->FillRoundedRectangle(
      D2D1::RoundedRect(D2D1::RectF(left, top, left + width, top + height), radius, radius),
      brush.Get());

  const float inset = 2.0f;
  const float thumbRadius = radius - inset;
  const float thumbX = controlStyle_.on ? left + width - radius : left + radius;
  ComPtr<ID2D1SolidColorBrush> thumbBrush;
  const D2D1_COLOR_F thumbColour =
      colourOr(controlStyle_.thumb, controlStyle_.hasThumb, D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f));
  if (FAILED(target->CreateSolidColorBrush(thumbColour, thumbBrush.GetAddressOf()))) {
    return;
  }
  thumbBrush->SetOpacity(controlStyle_.disabled ? 0.4f : 1.0f);
  target->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, top + radius), thumbRadius, thumbRadius),
                      thumbBrush.Get());
}

// Always drawn, rather than faded in while scrolling and out after: there is no
// timer here and no animation, which is why `flashScrollIndicators` stays a
// no-op -- there is nothing to flash something already on screen.
void RnWin32View::paintScrollIndicators(ID2D1RenderTarget *target) const {
  const float thickness = static_cast<float>(basalt::kScrollIndicatorThickness);
  const float inset = static_cast<float>(basalt::kScrollIndicatorInset);

  ComPtr<ID2D1SolidColorBrush> brush;
  // Neutral and translucent, so it reads over light and dark content alike --
  // the same colour the GTK and AppKit hosts use.
  if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.35f),
                                           brush.GetAddressOf()))) {
    return;
  }

  const float radius = thickness / 2.0f;
  if (indicatorVerticalLength_ > 0.0f) {
    const float left = frame_.width - thickness - inset;
    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(left,
                                      indicatorVerticalOffset_,
                                      left + thickness,
                                      indicatorVerticalOffset_ + indicatorVerticalLength_),
                          radius,
                          radius),
        brush.Get());
  }
  if (indicatorHorizontalLength_ > 0.0f) {
    const float top = frame_.height - thickness - inset;
    target->FillRoundedRectangle(
        D2D1::RoundedRect(D2D1::RectF(indicatorHorizontalOffset_,
                                      top,
                                      indicatorHorizontalOffset_ + indicatorHorizontalLength_,
                                      top + thickness),
                          radius,
                          radius),
        brush.Get());
  }
}

void RnWin32View::paintChildren(ID2D1RenderTarget *target) const {
  if (children_.empty()) {
    return;
  }

  D2D1::Matrix3x2F worldTransform;
  target->GetTransform(&worldTransform);

  // `overflow: hidden`, and only that: the background above is clipped whether
  // or not this is set.
  const D2D1_RECT_F bounds = D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height);
  ScopedGeometryClip clip(clipsChildren_ ? target : nullptr, bounds, cornerRadii_);

  // A ScrollView's offset moves its children and nothing else, so it belongs
  // between this view's transform and theirs.
  if (scrollX_ != 0.0f || scrollY_ != 0.0f) {
    target->SetTransform(D2D1::Matrix3x2F::Translation(-scrollX_, -scrollY_) * worldTransform);
  }

  // Forwards, so the last child painted is on top. Hit testing walks the same
  // list backwards.
  for (const RnWin32View *child : childrenInPaintOrder()) {
    child->paint(target);
  }
}

// --- Hit testing ------------------------------------------------------------

namespace {

// The spelling React Native uses for the prop, which is also CSS's, so the
// three hosts' dumps say the same words.
const char *pointerEventsName(RnWin32View::PointerEvents mode) {
  switch (mode) {
    case RnWin32View::PointerEvents::None:
      return "none";
    case RnWin32View::PointerEvents::BoxNone:
      return "box-none";
    case RnWin32View::PointerEvents::BoxOnly:
      return "box-only";
    case RnWin32View::PointerEvents::Auto:
      break;
  }
  return "auto";
}

} // namespace

void RnWin32View::setHitSlop(const float insets[4]) {
  bool any = false;
  for (int edge = 0; edge < 4; edge++) {
    hitSlop_[edge] = insets != nullptr ? insets[edge] : 0.0f;
    if (hitSlop_[edge] != 0.0f) {
      any = true;
    }
  }
  hasHitSlop_ = any;
}

RnWin32View *hitTest(RnWin32View *root, float x, float y) {
  // `pointerEvents: none` takes the view and everything inside it out of hit
  // testing entirely, so the caller's loop carries on to whatever is behind.
  if (root == nullptr || root->hidden() ||
      root->pointerEvents() == RnWin32View::PointerEvents::None) {
    return nullptr;
  }

  const RnRect &frame = root->frame();
  // `hitSlop` widens this test and only this test, so a press just outside the
  // box still lands on it. The slop is in this view's own coordinates, which is
  // where the point already is: a transform was inverted on the way down.
  //
  // The parent's answer is unchanged, so a slop reaching outside the parent is
  // only reachable where the parent is -- the same bound the other two hosts
  // have, and the same one iOS has, because each of them widens one view's test
  // rather than the walk that got here.
  const float *const slop = root->hitSlop();
  const float left = root->hasHitSlop() ? -slop[3] : 0.0f;
  const float top = root->hasHitSlop() ? -slop[0] : 0.0f;
  const float right = frame.width + (root->hasHitSlop() ? slop[1] : 0.0f);
  const float bottom = frame.height + (root->hasHitSlop() ? slop[2] : 0.0f);
  if (x < left || y < top || x >= right || y >= bottom) {
    return nullptr;
  }

  // Children are placed in this view's content space, which a scroll offset
  // shifts. Adding it back here is what makes hit testing follow a scroll with
  // nothing in this function knowing what a ScrollView is -- the same offset
  // `paintChildren` subtracts, from the same two fields.
  const float contentX = x + root->scrollX();
  const float contentY = y + root->scrollY();

  // Backwards: the last child painted is the topmost, and the topmost is what a
  // press should land on.
  //
  // `box-only` is the one mode that skips this: the box is the target and
  // nothing inside it is, which is what makes an overlay swallow a press meant
  // for a button drawn on top of it.
  if (root->pointerEvents() != RnWin32View::PointerEvents::BoxOnly) {
    const std::vector<RnWin32View *> ordered = root->childrenInPaintOrder();
    for (auto it = ordered.rbegin(); it != ordered.rend(); ++it) {
      RnWin32View *child = *it;
      float local[6];
      child->localToParent(local);

      float childX = 0.0f;
      float childY = 0.0f;
      if (!invertPoint(local, contentX, contentY, childX, childY)) {
        continue;
      }
      if (RnWin32View *hit = hitTest(child, childX, childY)) {
        return hit;
      }
    }
  }

  // `box-none` is transparent to a press that misses everything inside it.
  // Returning null rather than the parent is the whole of it: the caller is
  // partway through its own list of children, so the press carries on to the
  // sibling *behind* this view -- which is what the absolutely-positioned
  // overlay this mode exists for is asking for.
  if (root->pointerEvents() == RnWin32View::PointerEvents::BoxNone) {
    return nullptr;
  }

  // A point inside this view but over none of its children is this view. React
  // Native's responder system needs a target for every press inside the
  // surface, and the root is the honest answer for one that missed everything.
  return root;
}

// --- Reporting -------------------------------------------------------------

std::string RnWin32View::describeTree() const {
  std::string out;
  describeInto(out, 0);
  return out;
}

void RnWin32View::describeInto(std::string &out, int depth) const {
  for (int i = 0; i < depth; i++) {
    out += "  ";
  }

  appendFormat(out,
               "view tag=%d frame=(%g,%g %gx%g)",
               tag_,
               static_cast<double>(frame_.x),
               static_cast<double>(frame_.y),
               static_cast<double>(frame_.width),
               static_cast<double>(frame_.height));

  if (hasBackgroundColor_) {
    appendFormat(out,
                 " bg=#%02x%02x%02x%02x",
                 toByte(backgroundColor_[0]),
                 toByte(backgroundColor_[1]),
                 toByte(backgroundColor_[2]),
                 toByte(backgroundColor_[3]));
  }
  // Field order matches the GTK and AppKit sides exactly -- bg, opacity, clip,
  // transform, scroll, then the content fields -- because
  // scripts/compare_hosts.sh diffs the dumps line by line and a reordering
  // would read as every line differing.
  if (opacity_ < 1.0f) {
    appendFormat(out, " opacity=%g", static_cast<double>(opacity_));
  }
  if (clipsChildren_) {
    out += " clip";
  }
  // Whether anything is drawn at all. Without this a view hidden by
  // `display: none` or by a back face turned away reads exactly like a visible
  // one, and the only prop in this dump that removes a view entirely was the
  // only one it could not show.
  if (hidden_) {
    out += " hidden";
  }
  // Per-corner radii and per-edge borders, in the fields, the order and the
  // formatting GTK prints them -- RnView.cpp's describe -- so that
  // scripts/compare_hosts.sh compares what each host actually draws.
  //
  // Both were missing here until this host drew them: it printed one circular
  // radius eight times and no border at all, and printing nothing was what let
  // the comparison say so rather than hide it. Radii are eight numbers because
  // React Native's are elliptical: top-left, top-right, bottom-right,
  // bottom-left, horizontal then vertical.
  if (hasCornerRadii_) {
    appendFormat(out,
                 " radii=(%g,%g,%g,%g,%g,%g,%g,%g)",
                 static_cast<double>(cornerRadii_[0]),
                 static_cast<double>(cornerRadii_[1]),
                 static_cast<double>(cornerRadii_[2]),
                 static_cast<double>(cornerRadii_[3]),
                 static_cast<double>(cornerRadii_[4]),
                 static_cast<double>(cornerRadii_[5]),
                 static_cast<double>(cornerRadii_[6]),
                 static_cast<double>(cornerRadii_[7]));
  }
  // Top, right, bottom, left, for both.
  if (hasBorders_) {
    appendFormat(out,
                 " borderw=(%g,%g,%g,%g)",
                 static_cast<double>(borderWidths_[0]),
                 static_cast<double>(borderWidths_[1]),
                 static_cast<double>(borderWidths_[2]),
                 static_cast<double>(borderWidths_[3]));
    out += " borderc=(";
    for (int edge = 0; edge < 4; edge++) {
      const float *c = &borderColours_[edge * 4];
      appendFormat(out,
                   "%s#%02x%02x%02x%02x",
                   edge == 0 ? "" : ",",
                   toByte(c[0]),
                   toByte(c[1]),
                   toByte(c[2]),
                   toByte(c[3]));
    }
    out += ")";
  }
  if (hasTransform_) {
    // The 2D affine part, in the order CSS writes a matrix(): a, b, c, d, tx,
    // ty. The other two platforms print the same six from their own matrix
    // types, so a transform is comparable across all three -- without which a
    // view rotated on one desktop and not on another looks identical here.
    appendFormat(out,
                 " transform=(%g,%g,%g,%g,%g,%g)",
                 static_cast<double>(transform_[0]),
                 static_cast<double>(transform_[1]),
                 static_cast<double>(transform_[2]),
                 static_cast<double>(transform_[3]),
                 static_cast<double>(transform_[4]),
                 static_cast<double>(transform_[5]));
  }
  if (scrollX_ != 0.0f || scrollY_ != 0.0f) {
    appendFormat(out,
                 " scroll=(%g,%g)",
                 static_cast<double>(scrollX_),
                 static_cast<double>(scrollY_));
  }
  // The overlay scrollbars, which are otherwise pure paint and so invisible to
  // every test this project has. Printed only when there is one, so a view that
  // does not scroll stays as short as it was.
  if (indicatorVerticalLength_ > 0.0f) {
    appendFormat(out,
                 " scrollbar-v=(%g,%g)",
                 static_cast<double>(indicatorVerticalOffset_),
                 static_cast<double>(indicatorVerticalLength_));
  }
  if (indicatorHorizontalLength_ > 0.0f) {
    appendFormat(out,
                 " scrollbar-h=(%g,%g)",
                 static_cast<double>(indicatorHorizontalOffset_),
                 static_cast<double>(indicatorHorizontalLength_));
  }
  // Printed only when it is not the default, like every other field here.
  // Worth printing at all because it is invisible: a view with
  // `pointerEvents: none` is drawn exactly like one without, and the only way
  // the cross-host diff can say the prop arrived on all three is if each one
  // reports it.
  if (pointerEvents_ != PointerEvents::Auto) {
    appendFormat(out, " pe=%s", pointerEventsName(pointerEvents_));
  }

  if (image_ != nullptr) {
    // The same `texture=WxH fit=<name>` the other two hosts emit. The fit is
    // here because it is the only thing about a drawn image that a frame cannot
    // show: two views the same size holding the same picture are identical in
    // every other field of this dump and different on screen.
    appendFormat(out, " texture=%ux%u", image_->width(), image_->height());
    appendFormat(out, " fit=%s", imageFitName(imageFit_));
    // Printed for the same reason the fit is, and in the same format the other
    // two hosts use, so the cross-host diff can compare them.
    if (hasImageTint_) {
      appendFormat(out,
                   " tint=#%02x%02x%02x%02x",
                   static_cast<unsigned>(imageTint_[0] * 255.0f + 0.5f),
                   static_cast<unsigned>(imageTint_[1] * 255.0f + 0.5f),
                   static_cast<unsigned>(imageTint_[2] * 255.0f + 0.5f),
                   static_cast<unsigned>(imageTint_[3] * 255.0f + 0.5f));
    }
  }

  if (textLayout_ != nullptr) {
    const std::string &text = textLayout_->text();
    if (!text.empty()) {
      out += " text=\"";
      appendEscaped(out, text);
      out += "\"";
    }
    // The paragraph's writing direction, spelled as the other two hosts spell
    // it, and only when it is the one nothing else in this dump can show: a
    // right-to-left paragraph of Latin text has the same box and the same
    // string as a left-to-right one, and only the pixels differ.
    //
    // Left-to-right is not printed, where the other two print `ltr` and
    // `natural` as well. They keep the name the app used; this host keeps a
    // boolean, because DirectWrite takes a direction rather than a
    // "decide for me". So what is comparable is the line that matters.
    if (textLayout_->style().rightToLeft) {
      out += " writing-dir=rtl";
    }
    // The paragraph's text shadow, spelled as the other two hosts spell it: no
    // other line can show it, a shadowed paragraph having the same text, the
    // same colour and the same box. The standard deviation React Native parsed,
    // which is also what the shadow effect was given.
    if (textLayout_->hasShadow()) {
      const float *const colour = textLayout_->shadowColour();
      appendFormat(out,
                   " text-shadow=(%g,%g,%g,#%02x%02x%02x%02x)",
                   static_cast<double>(textLayout_->shadowDx()),
                   static_cast<double>(textLayout_->shadowDy()),
                   static_cast<double>(textLayout_->shadowStandardDeviation()),
                   static_cast<unsigned>(colour[0] * 255.0f + 0.5f),
                   static_cast<unsigned>(colour[1] * 255.0f + 0.5f),
                   static_cast<unsigned>(colour[2] * 255.0f + 0.5f),
                   static_cast<unsigned>(colour[3] * 255.0f + 0.5f));
    }
  }

  // A text field's content lives in its EDIT peer, not in a layout, so it would
  // otherwise be invisible to every test that reads this tree -- and the dump
  // would differ from the other two hosts' for a field that was working.
  if (editablePeer_ != nullptr) {
    const int length = GetWindowTextLength(editablePeer_);
    std::wstring wide(static_cast<size_t>(length) + 1, L'\0');
    GetWindowText(editablePeer_, wide.data(), length + 1);
    wide.resize(static_cast<size_t>(length));

    out += " editable=\"";
    appendEscaped(out, narrow(wide));
    out += "\"";

    // GetFocus is per-thread rather than global, which is the right question
    // here: this runs on the thread that owns the window, and what is being
    // asked is whether the field has the keyboard within it.
    if (GetFocus() == editablePeer_) {
      out += " focused";
    }
  }

  // How many DevTools highlights this view is drawing. In the dump because they
  // are otherwise invisible to everything but a screenshot, and because a
  // command that arrived and drew nothing is exactly the failure worth
  // catching.
  if (!highlights_.empty()) {
    appendFormat(out, " highlights=%d", static_cast<int>(highlights_.size()));
  }

  // What kind of control this view is, and what state it is in. Written by
  // core/DesktopControls.h rather than formatted here, for the same reason the
  // role name below is React Native's vocabulary and not UIA's: three hosts
  // describing the same switch in three ways is a diff on every line.
  if (!controlDescription_.empty()) {
    appendFormat(out, " control=%s", controlDescription_.c_str());
  }

  // The border's style, when it is not solid. Printed for the same reason the
  // widths are: a dashed border and a solid one are the same four widths and
  // the same four colours, and this is the only thing that can say the prop
  // arrived. Spelled as the other two hosts spell it.
  if (hasBorders_ && borderStyle_ != LineStyle::Solid) {
    appendFormat(out,
                 " border-style=%s",
                 borderStyle_ == LineStyle::Dotted ? "dotted" : "dashed");
  }

  // The outline, which is invisible in every other line: it is not a border,
  // and a view with one has the same frame and the same colours without it.
  // Spelled exactly as the other two hosts spell it, style included only when
  // it is not solid, because this line is diffed across the three.
  if (outlineWidth_ > 0.0f) {
    appendFormat(out,
                 " outline=(%g,%g,#%02x%02x%02x%02x",
                 static_cast<double>(outlineWidth_),
                 static_cast<double>(outlineOffset_),
                 static_cast<unsigned>(outlineColour_[0] * 255.0f + 0.5f),
                 static_cast<unsigned>(outlineColour_[1] * 255.0f + 0.5f),
                 static_cast<unsigned>(outlineColour_[2] * 255.0f + 0.5f),
                 static_cast<unsigned>(outlineColour_[3] * 255.0f + 0.5f));
    if (outlineStyle_ != LineStyle::Solid) {
      appendFormat(out, ",%s", outlineStyle_ == LineStyle::Dotted ? "dotted" : "dashed");
    }
    out += ")";
  }

  // `hitSlop`, which is invisible in every other line of this dump: a view with
  // a bigger target is drawn exactly like one without. Before `role=` because
  // that is where the other two hosts print it, and this dump is diffed line by
  // line.
  if (hasHitSlop_) {
    appendFormat(out,
                 " hit-slop=(%g,%g,%g,%g)",
                 static_cast<double>(hitSlop_[0]),
                 static_cast<double>(hitSlop_[1]),
                 static_cast<double>(hitSlop_[2]),
                 static_cast<double>(hitSlop_[3]));
  }

  // React Native's role name, not UIA's. This dump is compared line by line
  // across three platforms, and each reporting its own toolkit's vocabulary
  // would make every accessible view look like a difference. That the *UIA*
  // control type was really applied is asserted in
  // tests/test_win32_accessibility.cpp, which is where a platform question
  // belongs.
  if (!accessible_.role.empty()) {
    appendFormat(out, " role=%s", accessible_.role.c_str());
  }
  // `testID`, as React Native spelled it: the same division as the role above,
  // and that UIA really published it as the automation id is asserted in
  // tests/test_win32_accessibility.cpp.
  if (!accessible_.testId.empty()) {
    appendFormat(out, " testid=%s", accessible_.testId.c_str());
  }
  // Whether Tab stops here, after `role=` because that is where the other two
  // hosts print it and this dump is diffed line by line. Whether it is focused
  // *now* is deliberately not printed: that depends on what the window manager
  // did when the window opened, which is not a property of the platform and
  // would make this dump differ between two machines running the same app.
  if (focusable_) {
    out += " focusable";
  }
  // `accessibilityViewIsModal`, after `focusable` because that is where the
  // other two hosts print it and this dump is diffed line by line.
  if (accessible_.modal) {
    out += " modal";
  }

  out += "\n";

  for (const RnWin32View *child : children_) {
    child->describeInto(out, depth + 1);
  }
}

} // namespace basalt::win32
