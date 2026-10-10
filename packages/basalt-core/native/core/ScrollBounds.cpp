#include "ScrollBounds.h"

#include <algorithm>

namespace basalt {

ScrollRange scrollRangeFor(double containerLength,
                           double contentLength,
                           ScrollAxisInsets insets) {
  ScrollRange range;
  // Pulling *before* the content is what a leading inset buys, so the range
  // starts below zero rather than at it.
  range.minimum = -insets.leading;
  range.maximum = std::max(range.minimum, contentLength - containerLength + insets.trailing);
  return range;
}

ScrollAxisInsets centeringInsets(double containerLength, double contentLength) {
  const double slack = containerLength - contentLength;
  if (slack <= 0.0) {
    return {};
  }
  return ScrollAxisInsets{.leading = slack / 2.0, .trailing = slack / 2.0};
}

double clampScrollOffset(double offset,
                         double containerLength,
                         double contentLength,
                         ScrollAxisInsets insets) {
  const ScrollRange range = scrollRangeFor(containerLength, contentLength, insets);
  return std::clamp(offset, range.minimum, range.maximum);
}

} // namespace basalt
