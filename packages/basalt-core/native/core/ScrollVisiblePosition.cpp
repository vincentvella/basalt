#include "ScrollVisiblePosition.h"

#include <cmath>

namespace basalt {

namespace {

// Below this an adjustment is rounding rather than movement. Upstream's number,
// and the reason for having one at all is that layout arithmetic repeats to
// within a fraction of a point: a list that moved by that on every transaction
// would drift under the person reading it.
constexpr double kMinimumMove = 0.5;

} // namespace

ScrollPinnedChild firstVisibleChild(const std::vector<ScrollChildBox> &children,
                                    int minIndexForVisible,
                                    double offset) {
  if (children.empty()) {
    return {};
  }
  const auto first = static_cast<size_t>(minIndexForVisible < 0 ? 0 : minIndexForVisible);
  if (first >= children.size()) {
    // A `minIndexForVisible` past the end of the list, which a list that has
    // not rendered its rows yet produces. Nothing to watch rather than the last
    // child: the index is the app saying which children it does not want
    // watched.
    return {};
  }

  for (size_t index = first; index < children.size(); index++) {
    const ScrollChildBox &child = children[index];
    const bool visible = child.leading + child.length > offset;
    // The last child is watched whether or not it is visible: a list scrolled
    // past everything has no visible child, and watching nothing would mean
    // adjusting by nothing.
    if (visible || index + 1 == children.size()) {
      return ScrollPinnedChild{.has = true, .tag = child.tag, .leading = child.leading};
    }
  }
  return {};
}

ScrollVisibleAdjustment visiblePositionAdjustment(
    double previousLeading,
    double currentLeading,
    double offset,
    std::optional<int> autoscrollToStartThreshold) {
  const double delta = currentLeading - previousLeading;
  if (std::abs(delta) <= kMinimumMove) {
    return {};
  }

  ScrollVisibleAdjustment adjustment;
  adjustment.move = true;
  adjustment.delta = delta;
  // Measured against the offset as it stands, before the adjustment: the
  // question is where the person was looking, not where they are about to be
  // put. Upstream reads the offset into a local for exactly that reason.
  if (autoscrollToStartThreshold.has_value()
      && offset <= static_cast<double>(*autoscrollToStartThreshold)) {
    adjustment.autoscrollToStart = true;
  }
  return adjustment;
}

} // namespace basalt
