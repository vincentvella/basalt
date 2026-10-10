#include "RnWin32View.h"

#include "Backface.h"
#include "ControlMetrics.h"
#include "FocusRing.h"
#include "RnWin32Image.h"
#include "RnWin32TextLayout.h"
#include "ScrollIndicator.h"
#include "Win32Clip.h"
#include "Win32Offscreen.h"
#include "Win32Strings.h"

// Which frame an animated image is showing, and the curve a box shadow's corner
// grows by. Both shared with the other two hosts.
#include "ImageAnimation.h"
#include "ShadowShape.h"

// Before d2d1.h, which wants the base Windows types and does not pull them in
// itself. NOMINMAX and WIN32_LEAN_AND_MEAN come from the package's CMakeLists;
// without the first, windows.h defines `min` and `max` as macros and breaks
// <algorithm> below at a distance.
#include <windows.h>

#include <d2d1.h>
#include <d2d1_1.h>
#include <d2d1effects.h>
#include <d2d1helper.h>
#include <wrl/client.h>

// `UiaRaiseNotificationEvent`, which is the only call in UI Automation that
// speaks a string, and the provider interface it raises the event on.
// `uiautomation.h` is the umbrella header the accessible half already uses.
#include <uiautomation.h>

#include <algorithm>
#include <chrono>
#include <optional>
#include <utility>
#include <cmath>
#include <cstdint>
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

void RnWin32View::setScrollIndicatorColour(float red, float green, float blue, float alpha) {
  indicatorColour_[0] = red;
  indicatorColour_[1] = green;
  indicatorColour_[2] = blue;
  indicatorColour_[3] = alpha;
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

void RnWin32View::setImageBlur(float radius) {
  // Negative is no blur rather than a crash, which nothing stops an app from
  // sending: the other two hosts clamp it for the same reason, and GSK would
  // otherwise take it.
  imageBlur_ = radius > 0.0f ? radius : 0.0f;
}

void RnWin32View::setImage(std::shared_ptr<RnWin32Image> image, RnImageFit fit) {
  imageFit_ = fit;
  // An animation owns `image_`, and the mounting manager re-applies the first
  // frame on every mutation that touched this view, a layout-only one included.
  // Taking it would restart the GIF on every resize, so the fit is all that is
  // taken here.
  if (imageFrames_.size() > 1 && image == imageFrames_.front()) {
    return;
  }
  image_ = std::move(image);
}

void RnWin32View::setImageFrames(std::vector<std::shared_ptr<RnWin32Image>> frames,
                                 std::vector<unsigned> delaysMs,
                                 unsigned loopCount) {
  const bool same = frames.size() == imageFrames_.size() && !frames.empty()
      && frames.front() == imageFrames_.front() && loopCount == imageLoopCount_;
  if (same) {
    // The same animation again, which is what a layout-only mutation produces:
    // the loader hands back the vector it cached, so this compares the first
    // frame and keeps the animation's place rather than starting over.
    return;
  }

  imageFrames_ = std::move(frames);
  imageDelays_ = std::move(delaysMs);
  imageLoopCount_ = loopCount;
  imageFrame_ = 0;
  imageElapsedMs_ = 0.0;
  if (imageFrames_.size() < 2 || imageDelays_.size() != imageFrames_.size()) {
    imageFrames_.clear();
    imageDelays_.clear();
    return;
  }
  image_ = imageFrames_.front();
}

double RnWin32View::advanceImageAnimation(double milliseconds) {
  if (imageFrames_.size() < 2) {
    return 0.0;
  }
  if (milliseconds > 0.0) {
    imageElapsedMs_ += milliseconds;
  }

  const basalt::ImageAnimationStep step = basalt::imageAnimationStep(
      imageDelays_, imageLoopCount_, static_cast<std::uint64_t>(imageElapsedMs_));
  if (step.frame != imageFrame_ && step.frame < imageFrames_.size()) {
    imageFrame_ = step.frame;
    image_ = imageFrames_[step.frame];
  }
  return step.nextInMs;
}

bool RnWin32View::advanceImageAnimations(double milliseconds) {
  bool animating = advanceImageAnimation(milliseconds) > 0.0;
  for (RnWin32View *child : children_) {
    if (child != nullptr && child->advanceImageAnimations(milliseconds)) {
      animating = true;
    }
  }
  return animating;
}

bool RnWin32View::hasAnimatedImage() const {
  if (imageFrames_.size() > 1) {
    return true;
  }
  for (const RnWin32View *child : children_) {
    if (child != nullptr && child->hasAnimatedImage()) {
      return true;
    }
  }
  return false;
}

void RnWin32View::setPainter(std::shared_ptr<RnWin32Painter> painter) {
  painter_ = std::move(painter);
}

// --- Accessibility ----------------------------------------------------------

void RnWin32View::setAccessibleInfo(const RnAccessibleInfo &info) {
  accessible_ = info;
}

IRawElementProviderSimple *RnWin32View::createAccessibleProvider() const {
  // The relation is resolved here rather than stored resolved, because the
  // provider holds a snapshot and this is the moment it is taken: the view it
  // points at is alive now, and whatever its label says now is what a client
  // asking now should be told.
  if (!labelledBy_.empty() && labelledBy_.front() != nullptr) {
    RnAccessibleInfo info = accessible_;
    info.labelledBy =
        std::make_shared<const RnAccessibleInfo>(labelledBy_.front()->accessibleInfo());
    return basalt::win32::createAccessibleProvider(info);
  }
  return basalt::win32::createAccessibleProvider(accessible_);
}

void RnWin32View::setTextChecking(const char *spellCheck, const char *autoCorrect) {
  spellCheck_ = spellCheck != nullptr ? spellCheck : "";
  autoCorrect_ = autoCorrect != nullptr ? autoCorrect : "";
}

void RnWin32View::setInputHiding(bool caretHidden, bool contextMenuHidden) {
  caretHidden_ = caretHidden;
  contextMenuHidden_ = contextMenuHidden;
}

void RnWin32View::setInputKinds(const char *autoCapitalize, const char *keyboardType) {
  autoCapitalize_ = autoCapitalize != nullptr ? autoCapitalize : "";
  keyboardType_ = keyboardType != nullptr ? keyboardType : "";
}

void RnWin32View::setLabelledBy(std::vector<RnWin32View *> labels) {
  labelledBy_ = std::move(labels);
}

void RnWin32View::collectTextInto(std::string &out) const {
  if (textLayout_ != nullptr) {
    const std::string &text = textLayout_->text();
    if (!text.empty()) {
      if (!out.empty()) {
        out += ' ';
      }
      out += text;
    }
  }
  for (const RnWin32View *child : children_) {
    if (child != nullptr) {
      child->collectTextInto(out);
    }
  }
}

std::string RnWin32View::collectText() const {
  std::string out;
  collectTextInto(out);
  return out;
}

void RnWin32View::announce(const std::string &text, bool assertive) {
  if (text.empty()) {
    return;
  }
  lastAnnouncement_ = text;

  // A provider to raise it on, which is also the one thing UIA needs that the
  // other two hosts do not: there is no view object a client knows about, only
  // what a provider says. Released straight after -- the event carries the
  // string, and nothing keeps the element.
  ComPtr<IRawElementProviderSimple> provider;
  provider.Attach(createAccessibleProvider());
  if (!provider) {
    return;
  }

  // Real BSTRs rather than a cast wide pointer: a BSTR carries its length ahead
  // of its characters, and UIA reads that length. A `wchar_t *` cast to BSTR
  // works until something asks how long it is.
  const BSTR spoken = SysAllocString(widen(text).c_str());
  const BSTR activity = SysAllocString(std::to_wstring(tag_).c_str());
  if (spoken != nullptr && activity != nullptr) {
    // `NotificationProcessing_ImportantAll` for an assertive region and
    // `NotificationProcessing_All` for a polite one, which is as close as UIA's
    // four processing hints come to ARIA's two politeness levels: both say
    // "read every one of these", and the important variant jumps the queue.
    //
    // `NotificationKind_ActionCompleted` is the kind for "something finished
    // and here is the result", which is what a status line saying `Saved` is.
    // The activity id groups notifications from one source; this host uses the
    // view's tag, so two regions do not cancel each other out.
    UiaRaiseNotificationEvent(
        provider.Get(),
        NotificationKind_ActionCompleted,
        assertive ? NotificationProcessing_ImportantAll : NotificationProcessing_All,
        spoken,
        activity);
  }
  SysFreeString(spoken);
  SysFreeString(activity);
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

  // `filter`, which applies to this view and everything inside it, as CSS says.
  // That needs the subtree as a picture, so it goes through an offscreen bitmap
  // and an effect graph; everything else draws straight onto the target.
  //
  // A blended child needs the same picture for a different reason: a blend
  // takes the backdrop as an input, and the backdrop is this view's content
  // plus whatever was painted before that child. Both cases are "the subtree
  // into a bitmap first", which is what made `paintContents` worth splitting
  // out; a view with both takes the filter's path and the blend is skipped
  // inside it, see paintFiltered.
  if (filters_.needsEffects()) {
    paintFiltered(target);
  } else if (hasBlendedChild()) {
    paintBlendLayer(target);
  } else {
    paintContents(target);
  }

  target->SetTransform(parentTransform);
}

void RnWin32View::paintContents(ID2D1RenderTarget *target,
                               ID2D1BitmapRenderTarget *blendLayer) const {
  const D2D1_RECT_F bounds = D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height);

  // The transform this view's contents are drawn in, read rather than composed
  // because the two callers set it differently: `paint` has applied this view's
  // own matrix to the window's target, and `paintFiltered` draws into an
  // offscreen bitmap that starts at the identity because the whole subtree goes
  // there. Kept because `paintChildren` leaves a <ScrollView>'s offset applied
  // and what comes after it does not scroll.
  D2D1::Matrix3x2F contentTransform;
  target->GetTransform(&contentTransform);

  {
    // `filter`'s own `opacity()` multiplies the view's: CSS has two ways to ask
    // for the same thing and an app can use both. Folded in here rather than
    // being an effect of its own, which is what the GTK host does too.
    // No layer when the subtree is going through one of its own: a pushed
    // layer holds everything drawn inside it in an intermediate surface, and
    // the blended children below read the target's bitmap. `paintBlendLayer`
    // applies the opacity to that bitmap instead, which is the same picture.
    const ScopedOpacity fade(target,
                             blendLayer != nullptr ? 1.0f : opacity_ * filters_.opacity);

    // The outset shadows, behind everything this view draws: CSS puts them
    // under the background, which is also the only place they can go without
    // showing through a translucent one.
    paintBoxShadows(target, false);

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

    // The `backgroundImage` gradients, over the background colour and under the
    // content: CSS's background-image is above its background-color.
    paintGradients(target);

    // The inset shadows, over the background and under the content, which is
    // where CSS puts them. (AppKit gets this one wrong and says so: a sublayer
    // is above the view's own text.)
    paintBoxShadows(target, true);

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
                   hasImageTint_ ? imageTint_ : nullptr,
                   imageBlur_);
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

    paintChildren(target, blendLayer);

    // paintChildren leaves a ScrollView's offset applied, and nothing below
    // here scrolls with the content -- so put the transform back to this view's
    // own before drawing any of it.
    target->SetTransform(contentTransform);

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

void RnWin32View::setFilters(const Filters &filters) {
  filters_ = filters;
}

// `filter`, as an effect graph over this subtree's own picture.
//
// CSS applies a filter to an element *and its descendants*, so the subtree has
// to exist as an image before anything can be done to it: the contents go into
// a compatible bitmap, the effects run over that, and the result is drawn where
// the view is. The other two hosts get the same shape from their compositors,
// GSK by pushing nodes and Core Animation by holding filters on the layer.
//
// The order is the one `core/Filters.h` settled and backlog/correctness.md
// records: the colour matrix and the blur commute, so either way round is the
// same picture, and the drop shadows are outermost, cast by the blurred and
// recoloured result rather than recoloured themselves. That is CSS's
// `grayscale(1) drop-shadow(...)` and not the other order, which is the limit
// that header names.
//
// **A child that overflows a filtered view is cropped to the view's box here**,
// because the bitmap is the view's own size. GSK blurs the overflow because its
// node tree carries the subtree's real extent, which nothing in this host
// computes; backlog/platform-windows.md records it rather than leaving it to be
// discovered.
void RnWin32View::paintFiltered(ID2D1RenderTarget *target) const {
  ComPtr<ID2D1DeviceContext> context;
  // With a format that keeps alpha rather than the window's, which has none:
  // every one of these effects is defined on what is *not* covered as much as
  // on what is. See Win32Offscreen.h.
  const ComPtr<ID2D1BitmapRenderTarget> offscreen =
      createOffscreen(target, frame_.width, frame_.height);
  if (FAILED(target->QueryInterface(IID_PPV_ARGS(&context))) || !offscreen) {
    // No device context, no effects: the unfiltered picture is the honest
    // answer, since a filter that cannot be applied should not take the view
    // with it.
    paintContents(target);
    return;
  }

  offscreen->BeginDraw();
  offscreen->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  paintContents(offscreen.Get());
  if (FAILED(offscreen->EndDraw())) {
    paintContents(target);
    return;
  }

  ComPtr<ID2D1Bitmap> picture;
  if (FAILED(offscreen->GetBitmap(picture.GetAddressOf())) || !picture) {
    paintContents(target);
    return;
  }

  // The chain, built from the bitmap outwards. `source` is whatever the last
  // effect produced, or the bitmap when there is none. Each effect is held for
  // as long as the graph is drawn, which is why they are declared out here.
  ComPtr<ID2D1Effect> straighten;
  ComPtr<ID2D1Effect> matrix;
  ComPtr<ID2D1Effect> blur;
  ComPtr<ID2D1Image> source;
  picture->QueryInterface(IID_PPV_ARGS(&source));

  if (filters_.hasMatrix
      && SUCCEEDED(context->CreateEffect(CLSID_D2D1ColorMatrix, matrix.GetAddressOf()))
      && matrix) {
    // Direct2D multiplies a *row* vector by the matrix, so its m(i, j) is the
    // weight of input channel i in output channel j -- the transpose of core's,
    // whose rows are outputs. The fifth row is the offset.
    D2D1_MATRIX_5X4_F wanted{};
    float *const cells = &wanted._11;
    for (int input = 0; input < 4; input++) {
      for (int output = 0; output < 4; output++) {
        cells[input * 4 + output] = filters_.matrix[output * 4 + input];
      }
    }
    for (int output = 0; output < 4; output++) {
      cells[16 + output] = filters_.offset[output];
    }
    matrix->SetValue(D2D1_COLORMATRIX_PROP_COLOR_MATRIX, wanted);
    // Clamped, which is what every browser does with a matrix that leaves the
    // range.
    matrix->SetValue(D2D1_COLORMATRIX_PROP_CLAMP_OUTPUT, TRUE);

    // **CSS's filters are defined on unpremultiplied colour**, and getting that
    // took two measurements rather than a reading of the documentation.
    //
    // `D2D1_COLORMATRIX_ALPHA_MODE_STRAIGHT` sounds like the answer and is not:
    // with it set, a half-transparent red whose alpha the matrix forces opaque
    // came out dark red, which is 0.5 read as a colour rather than as a colour
    // times a coverage. So that setting does not mean "convert for me"; it
    // means "the data is already straight, leave the alpha alone".
    //
    // What does convert is `CLSID_D2D1UnPremultiply` ahead of the matrix, with
    // the matrix then told PREMULTIPLIED -- which, read the same way, means
    // "premultiply the result on the way out". An explicit `Premultiply` effect
    // after it premultiplied a second time, which the second measurement
    // caught: a half-transparent view with an *identity* matrix came back at a
    // quarter of its colour.
    //
    // Both tests are kept, and they pin the pair from opposite ends: one has an
    // opaque result, where premultiplying is a no-op and only the conversion on
    // the way in shows; the other is transparent throughout, where only the
    // conversion on the way out does.
    matrix->SetValue(D2D1_COLORMATRIX_PROP_ALPHA_MODE,
                     D2D1_COLORMATRIX_ALPHA_MODE_PREMULTIPLIED);
    if (SUCCEEDED(context->CreateEffect(CLSID_D2D1UnPremultiply, straighten.GetAddressOf()))
        && straighten) {
      straighten->SetInput(0, source.Get());
      source.Reset();
      straighten->GetOutput(source.GetAddressOf());
    }
    matrix->SetInput(0, source.Get());
    source.Reset();
    matrix->GetOutput(source.GetAddressOf());
  }

  if (filters_.blurRadius > 0.0f
      && SUCCEEDED(context->CreateEffect(CLSID_D2D1GaussianBlur, blur.GetAddressOf()))
      && blur) {
    // Half the radius is the standard deviation, which is the conversion both
    // other hosts make and `core/Filters.h` documents.
    blur->SetValue(D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION, filters_.blurRadius / 2.0f);
    blur->SetInput(0, source.Get());
    source.Reset();
    blur->GetOutput(source.GetAddressOf());
  }

  // The shadows first, so they are behind the picture, and in the order they
  // were written: the first is the one nearest the content, which is the order
  // `core/Filters.h` keeps them in.
  for (const FilterShadow &shadow : filters_.shadows) {
    if (shadow.colour[3] <= 0.0f) {
      continue;
    }
    ComPtr<ID2D1Effect> cast;
    if (FAILED(context->CreateEffect(CLSID_D2D1Shadow, cast.GetAddressOf())) || !cast) {
      continue;
    }
    cast->SetInput(0, source.Get());
    cast->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, shadow.standardDeviation);
    const D2D1_VECTOR_4F tint{
        shadow.colour[0], shadow.colour[1], shadow.colour[2], shadow.colour[3]};
    cast->SetValue(D2D1_SHADOW_PROP_COLOR, tint);
    context->DrawImage(cast.Get(), D2D1::Point2F(shadow.dx, shadow.dy));
  }

  context->DrawImage(source.Get(), D2D1::Point2F(0.0f, 0.0f));
}

void RnWin32View::setGradients(std::vector<Gradient> gradients) {
  gradients_ = std::move(gradients);
}

// The `backgroundImage` gradients, above the background colour and below the
// content, which is where CSS paints a background image.
//
// Back to front, the first in the list being the one on top, and clipped to the
// view's border box -- CSS's painting area -- which is also what keeps a
// gradient from squaring off a rounded corner.
//
// The geometry and the stops were resolved in the mounting manager against the
// *image's* size and offset to where the image goes, so this draws a gradient
// and does no CSS: `area` is the rectangle it fills and `tile` is the period it
// repeats in. An axis that does not repeat arrives with the painting area as
// its tile, which comes out drawn once; see core/BackgroundLayers.h.
void RnWin32View::paintGradients(ID2D1RenderTarget *target) const {
  if (gradients_.empty()) {
    return;
  }
  const D2D1_RECT_F box = D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height);

  for (size_t index = gradients_.size(); index > 0; index--) {
    const Gradient &gradient = gradients_[index - 1];
    if (gradient.stops.empty() || gradient.area[2] <= 0.0f || gradient.area[3] <= 0.0f) {
      continue;
    }

    std::vector<D2D1_GRADIENT_STOP> stops;
    stops.reserve(gradient.stops.size());
    for (const GradientStop &stop : gradient.stops) {
      stops.push_back(D2D1_GRADIENT_STOP{
          stop.offset,
          D2D1::ColorF(stop.colour[0], stop.colour[1], stop.colour[2], stop.colour[3])});
    }

    // `D2D1_GAMMA_2_2` interpolates in sRGB rather than in linear light, which
    // is what CSS says and what the other two hosts do: GSK and Core Graphics
    // both interpolate the values as given. Linear light would be defensible
    // and would make the same stylesheet a different picture here.
    //
    // Clamped at both ends, which is also CSS: past the last stop the gradient
    // is that stop's colour, and that is what fills the rest of the image's
    // rectangle -- the ending shape of a radial gradient is the radius, not the
    // edge of the box.
    ComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(target->CreateGradientStopCollection(stops.data(),
                                                    static_cast<UINT32>(stops.size()),
                                                    D2D1_GAMMA_2_2,
                                                    D2D1_EXTEND_MODE_CLAMP,
                                                    collection.GetAddressOf()))
        || !collection) {
      continue;
    }

    ComPtr<ID2D1Brush> brush;
    if (gradient.kind == Gradient::Kind::Radial) {
      ComPtr<ID2D1RadialGradientBrush> radial;
      // The centre and two radii straight from `core/Gradients.h`: Direct2D's
      // brush takes an ellipse where Core Graphics takes a circle and a
      // transform, so this is the host that needs no coordinate scaling.
      const D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES properties =
          D2D1::RadialGradientBrushProperties(
              D2D1::Point2F(gradient.centreX, gradient.centreY),
              D2D1::Point2F(0.0f, 0.0f),
              gradient.radiusX,
              gradient.radiusY);
      if (SUCCEEDED(target->CreateRadialGradientBrush(
              properties, collection.Get(), radial.GetAddressOf()))) {
        brush = radial;
      }
    } else {
      ComPtr<ID2D1LinearGradientBrush> linear;
      const D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES properties =
          D2D1::LinearGradientBrushProperties(
              D2D1::Point2F(gradient.startX, gradient.startY),
              D2D1::Point2F(gradient.endX, gradient.endY));
      if (SUCCEEDED(target->CreateLinearGradientBrush(
              properties, collection.Get(), linear.GetAddressOf()))) {
        brush = linear;
      }
    }
    if (!brush) {
      continue;
    }

    // Always clipped, radii or not: CSS clips a background to the border box,
    // and `backgroundSize` and `backgroundPosition` can both put the image
    // outside it.
    const ScopedGeometryClip clip(target, box, cornerRadii_);

    const D2D1_RECT_F area = D2D1::RectF(gradient.area[0],
                                         gradient.area[1],
                                         gradient.area[0] + gradient.area[2],
                                         gradient.area[1] + gradient.area[3]);
    if (!gradient.repeats || gradient.tile[2] <= 0.0f || gradient.tile[3] <= 0.0f) {
      target->FillRectangle(area, brush.Get());
      continue;
    }

    // Tiled. The gradient's own geometry sits where the first tile is, so each
    // tile moves the *brush* rather than the gradient: one brush, one fill per
    // tile, which is the shape the AppKit host settled on. GSK has a repeat
    // node and needs none of this.
    //
    // From the first tile that can reach the box, which may be above and left
    // of it: `backgroundPosition` can put the first tile anywhere, and the ones
    // before it still cover the box.
    const float width = gradient.tile[2];
    const float height = gradient.tile[3];
    const float firstX =
        gradient.tile[0] - std::ceil((gradient.tile[0] - box.left) / width) * width;
    const float firstY =
        gradient.tile[1] - std::ceil((gradient.tile[1] - box.top) / height) * height;
    for (float y = firstY; y < box.bottom; y += height) {
      for (float x = firstX; x < box.right; x += width) {
        brush->SetTransform(
            D2D1::Matrix3x2F::Translation(x - gradient.tile[0], y - gradient.tile[1]));
        // The image's rectangle inside this tile, moved with it: a tile is the
        // period and the image need not fill it.
        target->FillRectangle(
            D2D1::RectF(area.left + (x - gradient.tile[0]),
                        area.top + (y - gradient.tile[1]),
                        area.right + (x - gradient.tile[0]),
                        area.bottom + (y - gradient.tile[1])),
            brush.Get());
      }
    }
    brush->SetTransform(D2D1::Matrix3x2F::Identity());
  }
}

void RnWin32View::setBoxShadows(std::vector<BoxShadow> shadows) {
  boxShadows_ = std::move(shadows);
}

void RnWin32View::paintBoxShadows(ID2D1RenderTarget *target, bool inset) const {
  if (boxShadows_.empty()) {
    return;
  }
  // Back to front: CSS's first shadow is the one on top, so the list is walked
  // in reverse and the first one ends up painted last. Both other hosts do the
  // same with the same list.
  for (size_t index = boxShadows_.size(); index > 0; index--) {
    const BoxShadow &shadow = boxShadows_[index - 1];
    if (shadow.inset != inset) {
      continue;
    }
    // A shadow with no colour at all is one React Native could not parse, and
    // painting it black is worse than painting nothing. The other two hosts
    // skip it for the same reason.
    if (shadow.colour[3] <= 0.0f) {
      continue;
    }
    paintBoxShadow(target, shadow);
  }
}

// One box shadow: a shape, a blur of that shape's alpha, and a clip.
//
// GSK has a shadow node per kind whose arguments are CSS's, and AppKit has
// CALayer's shadow properties with a path; Direct2D has `CLSID_D2D1Shadow`,
// which takes what is drawn and hands back its alpha blurred and coloured. So
// the blur is the platform's and the work here is the geometry, which is the
// same geometry the AppKit half builds as a CGPath -- and the corner radii grow
// through `core/ShadowShape.h`'s curve on both, because a spread is not an
// addition.
//
// **An outset shadow never paints inside the box that casts it and an inset one
// never outside it**, which is CSS and is also what keeps a translucent
// background from showing the shadow underneath it. That is the clip, and for
// the outset case it is a shape no rectangle can express: everything around a
// rounded box. See `geometryWithHole`.
void RnWin32View::paintBoxShadow(ID2D1RenderTarget *target, const BoxShadow &shadow) const {
  ComPtr<ID2D1Factory> factory;
  target->GetFactory(factory.GetAddressOf());
  if (!factory) {
    return;
  }

  // CSS does not allow a negative blur radius and React Native parses one
  // anyway, so it is clamped here. The spread may be negative and is not: GSK
  // asserts the same pair, and a sabotage run that swapped the two took that
  // host's suite down with it.
  const float blur = shadow.blur > 0.0f ? shadow.blur : 0.0f;
  // Room around the box for everything the shadow can reach. A gaussian is
  // truncated at about three standard deviations, which is one and a half
  // radii; twice the radius is the usual slack and is what the room below is.
  const float room = std::abs(shadow.dx) + std::abs(shadow.dy)
      + std::abs(shadow.spread) + 2.0f * blur + 2.0f;
  const D2D1_RECT_F box = D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height);
  const D2D1_RECT_F around =
      D2D1::RectF(-room, -room, frame_.width + room, frame_.height + room);
  const float square[8] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f};

  // The shape that casts the shadow, and the clip that keeps it where CSS puts
  // it.
  ComPtr<ID2D1Geometry> caster;
  ComPtr<ID2D1Geometry> mask;
  float radii[8];
  if (!shadow.inset) {
    for (int corner = 0; corner < 8; corner++) {
      radii[corner] =
          static_cast<float>(basalt::spreadRadius(cornerRadii_[corner], shadow.spread));
    }
    const D2D1_RECT_F grown = D2D1::RectF(box.left - shadow.spread + shadow.dx,
                                          box.top - shadow.spread + shadow.dy,
                                          box.right + shadow.spread + shadow.dx,
                                          box.bottom + shadow.spread + shadow.dy);
    caster = roundedBoxGeometry(factory.Get(), grown, radii);
    mask = geometryWithHole(factory.Get(),
                            roundedBoxGeometry(factory.Get(), around, square).Get(),
                            roundedBoxGeometry(factory.Get(), box, cornerRadii_).Get());
  } else {
    // The hole the shadow spills in from: the box moved by the offset and
    // shrunk by the spread. An inset shadow with no offset is a ring and one
    // with an offset is a crescent, which falls out of this rather than being
    // arranged.
    for (int corner = 0; corner < 8; corner++) {
      radii[corner] =
          static_cast<float>(basalt::spreadRadius(cornerRadii_[corner], -shadow.spread));
    }
    D2D1_RECT_F hole = D2D1::RectF(box.left + shadow.spread + shadow.dx,
                                   box.top + shadow.spread + shadow.dy,
                                   box.right - shadow.spread + shadow.dx,
                                   box.bottom - shadow.spread + shadow.dy);
    // A spread wider than the box turns the hole inside out, which would draw
    // the shape rather than the hole.
    if (hole.right < hole.left) {
      hole.right = hole.left;
    }
    if (hole.bottom < hole.top) {
      hole.bottom = hole.top;
    }
    caster = geometryWithHole(factory.Get(),
                              roundedBoxGeometry(factory.Get(), around, square).Get(),
                              roundedBoxGeometry(factory.Get(), hole, radii).Get());
    mask = roundedBoxGeometry(factory.Get(), box, cornerRadii_);
  }
  if (!caster) {
    return;
  }

  const ScopedGeometryClip clip(mask ? target : nullptr, mask.Get());

  // The blur, which is an effect and so needs a device context and a bitmap to
  // run over: the shape goes into an offscreen the size of the box plus the
  // room, and `CLSID_D2D1Shadow` blurs that bitmap's alpha and colours it. The
  // shape is drawn in opaque black because only its alpha is read.
  ComPtr<ID2D1DeviceContext> context;
  if (blur > 0.0f && SUCCEEDED(target->QueryInterface(IID_PPV_ARGS(&context)))) {
    const ComPtr<ID2D1BitmapRenderTarget> offscreen =
        createOffscreen(target, frame_.width + 2.0f * room, frame_.height + 2.0f * room);
    ComPtr<ID2D1SolidColorBrush> ink;
    if (offscreen
        && SUCCEEDED(offscreen->CreateSolidColorBrush(D2D1::ColorF(0.0f, 0.0f, 0.0f, 1.0f),
                                                      ink.GetAddressOf()))) {
      offscreen->BeginDraw();
      offscreen->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
      offscreen->SetTransform(D2D1::Matrix3x2F::Translation(room, room));
      offscreen->FillGeometry(caster.Get(), ink.Get());
      offscreen->SetTransform(D2D1::Matrix3x2F::Identity());
      ComPtr<ID2D1Bitmap> drawn;
      ComPtr<ID2D1Effect> effect;
      if (SUCCEEDED(offscreen->EndDraw())
          && SUCCEEDED(offscreen->GetBitmap(drawn.GetAddressOf())) && drawn
          && SUCCEEDED(context->CreateEffect(CLSID_D2D1Shadow, effect.GetAddressOf()))
          && effect) {
        effect->SetInput(0, drawn.Get());
        // Half the radius is the standard deviation, which is the conversion
        // every blur in this host makes and the one React Native's iOS half
        // settled on.
        effect->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION, blur / 2.0f);
        const D2D1_VECTOR_4F tint{
            shadow.colour[0], shadow.colour[1], shadow.colour[2], shadow.colour[3]};
        effect->SetValue(D2D1_SHADOW_PROP_COLOR, tint);
        // Back where the shape was: it was drawn `room` in from the bitmap's
        // own origin so the blur had somewhere to go.
        context->DrawImage(effect.Get(), D2D1::Point2F(-room, -room));
        return;
      }
    }
  }

  // No blur asked for, or no effect to be had: the shape filled in the
  // shadow's colour. For a blur of zero that is the whole picture, and
  // otherwise it is the same fallback the text shadow takes -- a hard shadow
  // rather than none, since a prop that cannot be honoured should not take the
  // view with it.
  ComPtr<ID2D1SolidColorBrush> brush;
  const D2D1_COLOR_F colour =
      D2D1::ColorF(shadow.colour[0], shadow.colour[1], shadow.colour[2], shadow.colour[3]);
  if (SUCCEEDED(target->CreateSolidColorBrush(colour, brush.GetAddressOf()))) {
    target->FillGeometry(caster.Get(), brush.Get());
  }
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
  // Neutral and translucent, so it reads over light and dark content alike, and
  // white instead when `indicatorStyle` asked for it. Core decides which, so
  // the GTK and AppKit hosts answer the prop with the same colour.
  if (FAILED(target->CreateSolidColorBrush(D2D1::ColorF(indicatorColour_[0],
                                                        indicatorColour_[1],
                                                        indicatorColour_[2],
                                                        indicatorColour_[3]),
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

namespace {

// CSS's blend keyword as a Direct2D blend mode, or nothing for a keyword this
// host cannot blend.
//
// The keyword is what crosses the seam -- see core/BlendModes.h -- so this is
// the one place that turns it into Direct2D's vocabulary, as the GTK host turns
// it into a GskBlendMode and AppKit into a Core Image filter. Sixteen of the
// seventeen are the same word in both lists, which is why they are written out
// rather than mapped by position: the enum and CSS's list are in different
// orders, and a table by position reports a neighbour's blend.
//
// **`plus-lighter` is `LINEAR_DODGE`**, which is the choice AppKit made with
// `CILinearDodgeBlendMode` and for the same reason: the keyword means clamped
// addition and linear dodge is addition. That makes this the only one of the
// three hosts with all seventeen; GSK has no node for it and paints unblended.
std::optional<D2D1_BLEND_MODE> blendModeFor(const std::string &keyword) {
  if (keyword.empty() || keyword == "normal") {
    return std::nullopt;
  }
  if (keyword == "multiply") {
    return D2D1_BLEND_MODE_MULTIPLY;
  }
  if (keyword == "screen") {
    return D2D1_BLEND_MODE_SCREEN;
  }
  if (keyword == "overlay") {
    return D2D1_BLEND_MODE_OVERLAY;
  }
  if (keyword == "darken") {
    return D2D1_BLEND_MODE_DARKEN;
  }
  if (keyword == "lighten") {
    return D2D1_BLEND_MODE_LIGHTEN;
  }
  if (keyword == "color-dodge") {
    return D2D1_BLEND_MODE_COLOR_DODGE;
  }
  if (keyword == "color-burn") {
    return D2D1_BLEND_MODE_COLOR_BURN;
  }
  if (keyword == "hard-light") {
    return D2D1_BLEND_MODE_HARD_LIGHT;
  }
  if (keyword == "soft-light") {
    return D2D1_BLEND_MODE_SOFT_LIGHT;
  }
  if (keyword == "difference") {
    return D2D1_BLEND_MODE_DIFFERENCE;
  }
  if (keyword == "exclusion") {
    return D2D1_BLEND_MODE_EXCLUSION;
  }
  if (keyword == "hue") {
    return D2D1_BLEND_MODE_HUE;
  }
  if (keyword == "saturation") {
    return D2D1_BLEND_MODE_SATURATION;
  }
  if (keyword == "color") {
    return D2D1_BLEND_MODE_COLOR;
  }
  if (keyword == "luminosity") {
    return D2D1_BLEND_MODE_LUMINOSITY;
  }
  if (keyword == "plus-lighter") {
    return D2D1_BLEND_MODE_LINEAR_DODGE;
  }
  return std::nullopt;
}

} // namespace

void RnWin32View::setBlendMode(const char *name) {
  blendMode_ = name != nullptr ? name : "";
  // `normal` is "no blending", which is also what no keyword at all means. Held
  // as nothing so that the dump says nothing: `core/BlendModes.h` answers null
  // for it, so this is only reachable from a direct call, and the three hosts'
  // lines have to agree either way.
  if (blendMode_ == "normal") {
    blendMode_.clear();
  }
  blends_ = blendModeFor(blendMode_).has_value();
}

void RnWin32View::paintChildren(ID2D1RenderTarget *target,
                               ID2D1BitmapRenderTarget *blendLayer) const {
  if (children_.empty()) {
    return;
  }

  D2D1::Matrix3x2F worldTransform;
  target->GetTransform(&worldTransform);

  // `overflow: hidden`, and only that: the background above is clipped whether
  // or not this is set.
  //
  // **With a blended child it cannot be one clip around the whole walk.** A
  // clip here is a layer with a geometric mask -- see Win32Clip.h -- and a
  // pushed layer holds everything drawn inside it in an intermediate surface,
  // where the bitmap a blend reads as its backdrop cannot see it. So each child
  // gets its own clip instead, which paints the same picture: clipping a group
  // and clipping each of its members to the same box are the same thing. The
  // GTK half had to do this too, for a different reason -- there the pops that
  // close each blend happen between the children.
  const D2D1_RECT_F bounds = D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height);
  const bool clipsEachChild = blendLayer != nullptr && clipsChildren_;
  const ScopedGeometryClip clip(
      clipsChildren_ && !clipsEachChild ? target : nullptr, bounds, cornerRadii_);

  // A ScrollView's offset moves its children and nothing else, so it belongs
  // between this view's transform and theirs.
  const D2D1::Matrix3x2F childTransform = scrollX_ != 0.0f || scrollY_ != 0.0f
      ? D2D1::Matrix3x2F::Translation(-scrollX_, -scrollY_) * worldTransform
      : worldTransform;
  if (scrollX_ != 0.0f || scrollY_ != 0.0f) {
    target->SetTransform(childTransform);
  }

  // Forwards, so the last child painted is on top. Hit testing walks the same
  // list backwards.
  //
  // In paint order rather than insertion order, which matters twice over for a
  // blend: zIndex can differ from the list, and a blend's backdrop is what was
  // painted beneath it. The GTK half has to sort the blended children for the
  // same reason; here the order is simply the order they are blended in.
  for (const RnWin32View *child : childrenInPaintOrder()) {
    if (child == nullptr) {
      continue;
    }
    if (blendLayer != nullptr && child->blends() && !child->hidden_) {
      blendChildIntoLayer(blendLayer, child);
      continue;
    }
    if (clipsEachChild) {
      // The clip is in this view's own coordinates, so it is pushed with the
      // scroll offset off and the offset put back for the child itself.
      target->SetTransform(worldTransform);
      const ScopedGeometryClip own(target, bounds, cornerRadii_);
      target->SetTransform(childTransform);
      child->paint(target);
      continue;
    }
    child->paint(target);
  }
}

bool RnWin32View::hasBlendedChild() const {
  for (const RnWin32View *child : children_) {
    if (child != nullptr && child->blends() && !child->hidden_) {
      return true;
    }
  }
  return false;
}

// This view's subtree into an offscreen bitmap, so that a blended child has a
// backdrop to read, and then that bitmap onto the target.
//
// The backdrop is the one thing a blend needs and the one thing a view cannot
// see: `CLSID_D2D1Blend` takes two images, and what is beneath a child is
// whatever its parent painted before it. On GTK the parent pushes a blend node
// per blended child before it paints anything; here the parent paints into a
// bitmap and each blended child reads it. Same arrangement, different compositor.
//
// **The backdrop stops at this view**, which is the deviation CSS would not
// make and backlog/correctness.md records for GTK as well: CSS blends with the
// backdrop of the nearest stacking context, which for a plain <View> reaches
// past its parent. What is beneath this view is on the window and cannot be
// read back without copying the window; what is inside it is this bitmap. The
// AppKit host does blend with the whole layer tree, because Core Animation
// composites it, so the two differ and the end-to-end scenario is deliberately
// an arrangement where they agree.
void RnWin32View::paintBlendLayer(ID2D1RenderTarget *target) const {
  const ComPtr<ID2D1BitmapRenderTarget> layer =
      createOffscreen(target, frame_.width, frame_.height);
  if (!layer) {
    // No offscreen, no backdrop: the children paint unblended, which is the
    // same fallback a keyword this host cannot blend takes.
    paintContents(target);
    return;
  }

  layer->BeginDraw();
  layer->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  paintContents(layer.Get(), layer.Get());
  if (FAILED(layer->EndDraw())) {
    paintContents(target);
    return;
  }

  ComPtr<ID2D1Bitmap> picture;
  if (FAILED(layer->GetBitmap(picture.GetAddressOf())) || !picture) {
    paintContents(target);
    return;
  }

  // The view's own opacity, which `paintContents` did not apply because a
  // pushed layer would have hidden the backdrop from the children. On the
  // bitmap it is the same picture and one multiply rather than a layer.
  target->DrawBitmap(picture.Get(),
                     D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height),
                     opacity_ * filters_.opacity,
                     D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                     nullptr);
}

// One blended child, in place of painting it onto the layer.
//
// Three surfaces, and each is there for a reason Direct2D imposes. The child
// goes into one of its own because the blend wants it as an image rather than
// as a draw. The result goes into another because the backdrop is the layer's
// own bitmap, and Direct2D will not read a bitmap it is drawing into. And the
// layer is then cleared and the result drawn back, rather than drawn over,
// because the result already contains the backdrop: drawing it over would
// composite the backdrop with itself.
void RnWin32View::blendChildIntoLayer(ID2D1BitmapRenderTarget *layer,
                                      const RnWin32View *child) const {
  const std::optional<D2D1_BLEND_MODE> mode = blendModeFor(child->blendMode());
  ComPtr<ID2D1DeviceContext> context;
  if (!mode.has_value() || FAILED(layer->QueryInterface(IID_PPV_ARGS(&context)))) {
    // A keyword Direct2D has no mode for, or no device context to run an effect
    // on: the child paints unblended rather than not at all. The dump still
    // reports the keyword, which is the rule GTK follows for `plus-lighter`.
    child->paint(layer);
    return;
  }

  // What is beneath the child: this view's own content and the children painted
  // before it, which is exactly what the layer holds at this point.
  layer->Flush();
  ComPtr<ID2D1Bitmap> backdrop;
  if (FAILED(layer->GetBitmap(backdrop.GetAddressOf())) || !backdrop) {
    child->paint(layer);
    return;
  }

  // The child's own picture, in this view's coordinates: the transform the
  // layer is in, which carries a <ScrollView>'s offset, so the child lands
  // where it would have been painted.
  D2D1::Matrix3x2F transform;
  layer->GetTransform(&transform);
  const ComPtr<ID2D1BitmapRenderTarget> top =
      createOffscreen(layer, frame_.width, frame_.height);
  ComPtr<ID2D1Bitmap> topBitmap;
  if (!top) {
    child->paint(layer);
    return;
  }
  top->BeginDraw();
  top->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  {
    // `overflow: hidden` clips the child here rather than where the blend
    // lands, which is what lets the result be drawn back over the whole layer:
    // outside the clip the child is transparent, a blend with nothing on top is
    // the backdrop, and the result is the layer unchanged there. The clip goes
    // on with the scroll offset off, as it does in paintChildren, and the
    // layer's own transform is what that offset was applied to.
    const D2D1::Matrix3x2F unscrolled =
        D2D1::Matrix3x2F::Translation(scrollX_, scrollY_) * transform;
    top->SetTransform(unscrolled);
    const ScopedGeometryClip clip(clipsChildren_ ? top.Get() : nullptr,
                                  D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height),
                                  cornerRadii_);
    top->SetTransform(transform);
    child->paint(top.Get());
  }
  if (FAILED(top->EndDraw()) || FAILED(top->GetBitmap(topBitmap.GetAddressOf()))
      || !topBitmap) {
    child->paint(layer);
    return;
  }

  const ComPtr<ID2D1BitmapRenderTarget> blended =
      createOffscreen(layer, frame_.width, frame_.height);
  ComPtr<ID2D1DeviceContext> blendedContext;
  ComPtr<ID2D1Effect> blend;
  ComPtr<ID2D1Bitmap> result;
  if (!blended || FAILED(blended->QueryInterface(IID_PPV_ARGS(&blendedContext)))
      || FAILED(blendedContext->CreateEffect(CLSID_D2D1Blend, blend.GetAddressOf()))
      || !blend) {
    child->paint(layer);
    return;
  }
  blend->SetInput(0, backdrop.Get());
  blend->SetInput(1, topBitmap.Get());
  blend->SetValue(D2D1_BLEND_PROP_MODE, mode.value());
  blended->BeginDraw();
  blended->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  blendedContext->SetTransform(D2D1::Matrix3x2F::Identity());
  blendedContext->DrawImage(blend.Get(), D2D1::Point2F(0.0f, 0.0f));
  if (FAILED(blended->EndDraw()) || FAILED(blended->GetBitmap(result.GetAddressOf()))
      || !result) {
    child->paint(layer);
    return;
  }

  // Back onto the layer, in its own coordinates rather than the scrolled ones:
  // the result is already in this view's space, and it is the whole layer --
  // backdrop included -- which is why the layer is cleared first rather than
  // drawn over.
  layer->SetTransform(D2D1::Matrix3x2F::Identity());
  layer->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));
  layer->DrawBitmap(result.Get(),
                    D2D1::RectF(0.0f, 0.0f, frame_.width, frame_.height),
                    1.0f,
                    D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                    nullptr);
  layer->SetTransform(transform);
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

void RnWin32View::setCursor(const char *name) {
  cursor_ = name != nullptr ? name : "";
}

std::string cursorNameAt(RnWin32View *root, float x, float y) {
  // The innermost view under the point, which is the same answer a press gets:
  // one hit test for both means a cursor cannot disagree with what a click will
  // do. `pointerEvents: none` is skipped by it, which is right here too -- a
  // view a press passes through should not be changing the pointer either.
  RnWin32View *view = hitTest(root, x, y);
  // And then up, because CSS's cursor inherits: a <Text> inside a button that
  // asked for `pointer` shows the hand, having asked for nothing itself.
  while (view != nullptr) {
    if (!view->cursor().empty()) {
      return view->cursor();
    }
    view = view->parent();
  }
  return {};
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
  // Gradients: how many, each one's line and stop count, where the image goes
  // and the tile it repeats in. Not every stop, which a transition hint can
  // turn into eleven of: what an end-to-end run needs is that the same gradient
  // arrived with the same geometry, and the stop fixup itself is asserted in
  // core's own tests. Spelled exactly as the other two hosts spell it.
  for (const Gradient &gradient : gradients_) {
    if (gradient.kind == Gradient::Kind::Radial) {
      appendFormat(out,
                   " gradient=(radial (%g,%g) %gx%g,%u stops",
                   static_cast<double>(gradient.centreX),
                   static_cast<double>(gradient.centreY),
                   static_cast<double>(gradient.radiusX),
                   static_cast<double>(gradient.radiusY),
                   static_cast<unsigned>(gradient.stops.size()));
    } else {
      appendFormat(out,
                   " gradient=((%g,%g)-(%g,%g),%u stops",
                   static_cast<double>(gradient.startX),
                   static_cast<double>(gradient.startY),
                   static_cast<double>(gradient.endX),
                   static_cast<double>(gradient.endY),
                   static_cast<unsigned>(gradient.stops.size()));
    }
    // `at=` is the rectangle the image fills and `tile=` is the period, printed
    // only when it repeats -- so a `no-repeat` background is the line without a
    // tile. The three background props are invisible in every other line here:
    // they move and repeat the image without changing the view at all.
    appendFormat(out,
                 ",at=(%g,%g %gx%g)",
                 static_cast<double>(gradient.area[0]),
                 static_cast<double>(gradient.area[1]),
                 static_cast<double>(gradient.area[2]),
                 static_cast<double>(gradient.area[3]));
    if (gradient.repeats) {
      appendFormat(out,
                   ",tile=(%g,%g %gx%g)",
                   static_cast<double>(gradient.tile[0]),
                   static_cast<double>(gradient.tile[1]),
                   static_cast<double>(gradient.tile[2]),
                   static_cast<double>(gradient.tile[3]));
    }
    out += ")";
  }

  // Box shadows, each in full and in the order the app wrote them. Nothing else
  // in this dump can say a shadow is there, and the numbers are the whole
  // feature: an offset that went to the wrong axis or a spread read as a blur
  // still draws a plausible shadow. Spelled exactly as the other two hosts
  // spell it, `inset ` included, because this line is read by one end-to-end
  // scenario on all three.
  for (const BoxShadow &shadow : boxShadows_) {
    appendFormat(out,
                 " shadow=(%s%g,%g,%g,%g,#%02x%02x%02x%02x)",
                 shadow.inset ? "inset " : "",
                 static_cast<double>(shadow.dx),
                 static_cast<double>(shadow.dy),
                 static_cast<double>(shadow.blur),
                 static_cast<double>(shadow.spread),
                 toByte(shadow.colour[0]),
                 toByte(shadow.colour[1]),
                 toByte(shadow.colour[2]),
                 toByte(shadow.colour[3]));
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
  // The thumb's colour, printed only when it is not the default black: a white
  // one is `indicatorStyle` having arrived, and the colour is the only part of
  // that a tree dump can see.
  if (indicatorColour_[0] != 0.0f || indicatorColour_[1] != 0.0f || indicatorColour_[2] != 0.0f) {
    appendFormat(out,
                 " scrollbar-colour=(%g,%g,%g,%g)",
                 static_cast<double>(indicatorColour_[0]),
                 static_cast<double>(indicatorColour_[1]),
                 static_cast<double>(indicatorColour_[2]),
                 static_cast<double>(indicatorColour_[3]));
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
    // That this image moves, which no other line can show: an animated GIF and
    // its first frame are the same size and the same picture in a snapshot. Not
    // which frame, deliberately: the three hosts tick on their own clocks, so a
    // cross-host diff of that would be a race, and each suite asserts the
    // frames itself.
    if (imageFrames_.size() > 1) {
      out += " animated=1";
    }
    // And the blur, spelled as the other two hosts spell it. Invisible in this
    // dump otherwise: a blurred image has the same frame, the same texture and
    // the same fit as a sharp one.
    if (imageBlur_ > 0.0f) {
      appendFormat(out, " blur=%g", static_cast<double>(imageBlur_));
    }
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

  // What a field asked for about spelling. Neither word appears for a field
  // that said nothing, which is the third state rather than a default; what
  // each host then did with it is in its own suite and on the support page.
  // This one can honour neither: a classic EDIT has no spell checker.
  if (!spellCheck_.empty()) {
    appendFormat(out, " spellcheck=%s", spellCheck_.c_str());
  }
  if (!autoCorrect_.empty()) {
    appendFormat(out, " autocorrect=%s", autoCorrect_.c_str());
  }
  // `autoCapitalize` and `keyboardType`, printed as the app wrote them so the
  // three dumps compare. This host turns the second into an input scope and the
  // first into a style bit for `characters` only; see Win32TextInput.cpp.
  if (!autoCapitalize_.empty()) {
    appendFormat(out, " autocapitalize=%s", autoCapitalize_.c_str());
  }
  if (!keyboardType_.empty()) {
    appendFormat(out, " keyboard=%s", keyboardType_.c_str());
  }
  // `caretHidden` and `contextMenuHidden`, printed only when asked for. Each is
  // the absence of something -- a blink, a menu -- so there is nothing else for
  // a test or the cross-host diff to look at.
  if (caretHidden_) {
    appendFormat(out, " caret=hidden");
  }
  if (contextMenuHidden_) {
    appendFormat(out, " context-menu=hidden");
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

  // The cursor the app asked for, which is invisible in a frame: the pointer is
  // not part of the picture. Printed for the reason `pointerEvents` is, and in
  // CSS's words rather than Win32's, so the three hosts' lines agree.
  if (!cursor_.empty()) {
    appendFormat(out, " cursor=%s", cursor_.c_str());
  }

  // The blend mode the app asked for, which a frame cannot show unless there is
  // something beneath it. The keyword rather than Direct2D's mode, so the line
  // is comparable with the other two hosts', and printed even for a keyword
  // this host cannot blend: the dump says what was asked for.
  if (!blendMode_.empty()) {
    appendFormat(out, " blend=%s", blendMode_.c_str());
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

  // `filter`, as the pieces it came to plus what its matrix makes of one probe
  // colour, which is the format the other two hosts print and the end-to-end
  // run reads. Sixteen numbers would drown the line; one colour is eight
  // characters and still fails when a matrix is wrong. The probe is
  // (1, 0.5, 0.25) so that no two channels can be swapped without the answer
  // changing.
  if (!filters_.empty()) {
    out += " filter=(";
    bool first = true;
    if (filters_.hasMatrix) {
      const float probe[4] = {1.0f, 0.5f, 0.25f, 1.0f};
      float result[4] = {0.0f, 0.0f, 0.0f, 0.0f};
      for (int row = 0; row < 4; row++) {
        result[row] = filters_.offset[row];
        for (int column = 0; column < 4; column++) {
          result[row] += filters_.matrix[row * 4 + column] * probe[column];
        }
      }
      const auto byte = [](float value) {
        const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
        return static_cast<unsigned>(clamped * 255.0f + 0.5f);
      };
      appendFormat(out,
                   "probe=#%02x%02x%02x%02x",
                   byte(result[0]),
                   byte(result[1]),
                   byte(result[2]),
                   byte(result[3]));
      first = false;
    }
    if (filters_.blurRadius > 0.0f) {
      appendFormat(out,
                   "%sblur=%g",
                   first ? "" : ",",
                   static_cast<double>(filters_.blurRadius));
      first = false;
    }
    if (filters_.opacity < 1.0f) {
      appendFormat(out,
                   "%sopacity=%g",
                   first ? "" : ",",
                   static_cast<double>(filters_.opacity));
      first = false;
    }
    // Each drop shadow, with the standard deviation React Native parsed: that
    // is the number all three hosts are handed, so it is the one a cross-host
    // diff should compare.
    for (const FilterShadow &shadow : filters_.shadows) {
      appendFormat(out,
                   "%sshadow=(%g,%g,%g,#%02x%02x%02x%02x)",
                   first ? "" : ",",
                   static_cast<double>(shadow.dx),
                   static_cast<double>(shadow.dy),
                   static_cast<double>(shadow.standardDeviation),
                   static_cast<unsigned>(shadow.colour[0] * 255.0f + 0.5f),
                   static_cast<unsigned>(shadow.colour[1] * 255.0f + 0.5f),
                   static_cast<unsigned>(shadow.colour[2] * 255.0f + 0.5f),
                   static_cast<unsigned>(shadow.colour[3] * 255.0f + 0.5f));
      first = false;
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

  // The resolved LABELLED_BY relation, by tag. The ids the app wrote are in its
  // own source; what is worth reporting is that they were resolved, and the
  // tags are Fabric's, so the three hosts print the same ones.
  if (!labelledBy_.empty()) {
    out += " labelled-by=";
    bool first = true;
    for (const RnWin32View *label : labelledBy_) {
      if (label == nullptr) {
        continue;
      }
      appendFormat(out, "%s%d", first ? "" : ",", label->tag());
      first = false;
    }
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
