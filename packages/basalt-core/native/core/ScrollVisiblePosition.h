// `maintainVisibleContentPosition`: keeping the content still while items are
// added above it.
//
// A chat list is the case the prop exists for. Messages arrive at the top of a
// list that is scrolled to the bottom, or older ones are prepended when the
// person pulls up, and without this the content under their eyes jumps by the
// height of whatever was inserted. React Native's answer is to measure one
// child before the mutation and again after, and to move the scroll offset by
// however far that child moved -- so the child stays where it was and the
// insertion happens off screen.
//
// ## Why this is arithmetic and not a host's business
//
// The whole of it is: which child to watch, how far it moved, and whether the
// list was near enough to the start to be taken there instead. None of that
// needs a toolkit, and all three hosts would otherwise spell it themselves --
// which is how the three scroll ranges and three snapping rules started, before
// `core/ScrollBounds.h` and `core/ScrollSnap.h`.
//
// What a host does provide is the children's boxes before and after, which is
// the one part a toolkit knows and this does not.
//
// ## The rules, which are upstream's
//
// `RCTScrollViewComponentView` does this in two halves, around the mounting
// transaction: `_prepareForMaintainVisibleScrollPosition` before and
// `_adjustForMaintainVisibleContentPosition` after. The three rules worth
// naming, each of which has a test:
//
//   - **The child watched is the first one that is even partly visible**, from
//     `minIndexForVisible` onwards: the first whose trailing edge is past the
//     offset. A list may keep a header out of this with that index, which is
//     what it is for.
//   - **The last child is the fallback.** When everything is scrolled past,
//     there is no visible child to watch and the last one is watched anyway;
//     without that a list scrolled to its end would adjust by nothing.
//   - **Half a point is not a move.** Layout arithmetic repeats to within a
//     rounding error, and an adjustment of a fraction of a pixel on every
//     transaction would be a list that drifts.
//
// And `autoscrollToTopThreshold`, which is the opposite behaviour and reads
// oddly until the case is named: a chat list sitting *at* the top, where new
// messages arrive, should follow them rather than hold still. So a list within
// that many points of the start is taken to the start instead.

#pragma once

#include <optional>
#include <vector>

namespace basalt {

// One child of the content view, on the axis that scrolls. `leading` is its
// top (or left) in the content's own coordinates and `length` its height (or
// width).
//
// The tag travels with it because the child watched has to be found again after
// the mutation, and its position in the list is exactly what may have changed.
struct ScrollChildBox {
  int tag{0};
  double leading{0.0};
  double length{0.0};
};

// The child a scroll view is watching, remembered across a transaction.
struct ScrollPinnedChild {
  bool has{false};
  int tag{0};
  double leading{0.0};
};

// Which child to watch, given the children before the mutation.
//
// Empty when there are no children at or after `minIndexForVisible`, which is
// also what an empty list gives: there is nothing to keep still.
ScrollPinnedChild firstVisibleChild(const std::vector<ScrollChildBox> &children,
                                   int minIndexForVisible,
                                   double offset);

// What to do once the mutation has happened and the watched child has moved.
struct ScrollVisibleAdjustment {
  // False when nothing moved far enough to be worth moving the list for.
  bool move{false};
  // How far the offset should change, signed the same way the offset is.
  double delta{0.0};
  // `autoscrollToTopThreshold`: the list was near the start, so it follows the
  // new content to the start rather than holding its place. The caller animates
  // this one, where the adjustment above is instant -- which is upstream's
  // division too.
  bool autoscrollToStart{false};
};

// `previousLeading` is where the watched child was before the mutation and
// `currentLeading` where it is now; `offset` is the scroll offset as it stands,
// which is the one the threshold is measured against.
ScrollVisibleAdjustment visiblePositionAdjustment(double previousLeading,
                                                  double currentLeading,
                                                  double offset,
                                                  std::optional<int> autoscrollToStartThreshold);

} // namespace basalt
