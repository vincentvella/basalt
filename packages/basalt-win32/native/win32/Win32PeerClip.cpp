#include "Win32PeerClip.h"

#include "RnWin32View.h"

#include <algorithm>
#include <cmath>

namespace basalt::win32 {

namespace {

RECT boxOf(double x, double y, double width, double height) {
  RECT rect{};
  rect.left = static_cast<LONG>(std::lround(x));
  rect.top = static_cast<LONG>(std::lround(y));
  rect.right = rect.left + static_cast<LONG>(std::lround(width));
  rect.bottom = rect.top + static_cast<LONG>(std::lround(height));
  return rect;
}

bool isEmpty(const RECT &rect) {
  return rect.right <= rect.left || rect.bottom <= rect.top;
}

RECT intersect(const RECT &a, const RECT &b) {
  RECT out{};
  out.left = std::max(a.left, b.left);
  out.top = std::max(a.top, b.top);
  out.right = std::min(a.right, b.right);
  out.bottom = std::min(a.bottom, b.bottom);
  if (isEmpty(out)) {
    return RECT{0, 0, 0, 0};
  }
  return out;
}

// Whether this view puts something over the whole of its own box.
//
// A background colour with any alpha at all, and nothing else -- see the
// header for why the bar is "not transparent" rather than "opaque", and why a
// transformed view is left alone instead of being approximated by its bounding
// box.
bool covers(const RnWin32View *view) {
  if (!view->hasBackgroundColor() || view->hasTransform()) {
    return false;
  }
  if (view->opacity() <= 0.0f) {
    return false;
  }
  return view->backgroundColor()[3] > 0.0f;
}

struct Walk {
  const RnWin32View *target{nullptr};
  PeerClip *out{nullptr};
  // Set when the target has been reached, which is what divides the tree into
  // "painted before it", which cannot cover it, and "painted after it", which
  // can. One flag is enough because painting order is a single sequence.
  bool seen{false};
};

// `origin` is where this view's parent puts its children, already scrolled.
void visit(RnWin32View *view, double originX, double originY, const RECT &clip, Walk &walk) {
  if (view == nullptr || view->hidden()) {
    return;
  }

  const double x = originX + view->frame().x;
  const double y = originY + view->frame().y;
  const RECT box = boxOf(x, y, view->frame().width, view->frame().height);

  if (view == walk.target) {
    const RECT visible = intersect(box, clip);
    if (isEmpty(visible)) {
      return;
    }
    walk.out->visible = true;
    walk.out->bounds = box;
    walk.out->clip = visible;
    // Its own subtree is not walked: a child of a `<TextInput>` is inside the
    // field rather than over it, and the field has none in any real tree.
    walk.seen = true;
    return;
  }

  if (walk.seen && covers(view)) {
    const RECT over = intersect(box, clip);
    if (!isEmpty(over)) {
      walk.out->occluders.push_back(over);
    }
  }

  RECT childClip = clip;
  if (view->clipsChildren()) {
    childClip = intersect(clip, box);
    if (isEmpty(childClip)) {
      // Nothing inside can be seen, and nothing inside can cover anything
      // either -- but the target may still be in there, and if it is then it is
      // off screen, which is what not finding it already means.
      return;
    }
  }

  const double childX = x - view->scrollX();
  const double childY = y - view->scrollY();
  for (RnWin32View *child : view->childrenInPaintOrder()) {
    visit(child, childX, childY, childClip, walk);
  }
}

} // namespace

PeerClip peerClip(RnWin32View *view, RnWin32View *root) {
  PeerClip result{};
  if (view == nullptr || root == nullptr) {
    return result;
  }

  // The root's own frame is not part of these coordinates: the surface root is
  // the client area, and a peer's position is measured from its top-left. So
  // the walk starts inside it rather than at it, which also means a `view` that
  // *is* the root is not a field and comes back invisible.
  if (root->hidden()) {
    return result;
  }

  // Unbounded unless the root says otherwise, which is the rule every other
  // ancestor is under. Clipping to the client area regardless would be defen-
  // sible and is not done here: a surface that has never been laid out has a
  // zero frame, and "every field is invisible" is a worse answer than "a field
  // outside the window is placed outside the window", which USER32 handles.
  constexpr LONG kUnbounded = 1 << 28;
  RECT clip{-kUnbounded, -kUnbounded, kUnbounded, kUnbounded};
  if (root->clipsChildren()) {
    clip = boxOf(0.0, 0.0, root->frame().width, root->frame().height);
    if (isEmpty(clip)) {
      return result;
    }
  }
  Walk walk{view, &result, false};
  const double childX = -static_cast<double>(root->scrollX());
  const double childY = -static_cast<double>(root->scrollY());
  for (RnWin32View *child : root->childrenInPaintOrder()) {
    visit(child, childX, childY, clip, walk);
  }

  if (!result.visible) {
    return PeerClip{};
  }
  return result;
}

} // namespace basalt::win32
