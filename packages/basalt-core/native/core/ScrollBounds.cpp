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

double clampScrollOffset(double offset,
                         double containerLength,
                         double contentLength,
                         ScrollAxisInsets insets) {
  const ScrollRange range = scrollRangeFor(containerLength, contentLength, insets);
  return std::clamp(offset, range.minimum, range.maximum);
}

} // namespace basalt
