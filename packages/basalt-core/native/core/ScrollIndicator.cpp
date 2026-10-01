#include "ScrollIndicator.h"

#include <algorithm>

namespace basalt {

ScrollIndicator scrollIndicatorFor(double containerLength,
                                   double contentLength,
                                   double scrollOffset,
                                   ScrollAxisInsets content,
                                   ScrollAxisInsets indicator) {
  ScrollIndicator result;
  if (containerLength <= 0.0) {
    return result;
  }

  // Nothing to indicate when there is nowhere to go. Asked of the range rather
  // than of the content, because a `contentInset` is somewhere to go: a list
  // that fits exactly but can be pulled down past its top does scroll, and a
  // bar is how you know that.
  const ScrollRange range = scrollRangeFor(containerLength, contentLength, content);
  if (range.length() <= 0.0) {
    return result;
  }

  // The track: the view, less the standard inset at each end, less whatever
  // `scrollIndicatorInsets` asks for on top of that.
  const double start = kScrollIndicatorInset + indicator.leading;
  const double end = containerLength - kScrollIndicatorInset - indicator.trailing;
  const double track = end - start;
  if (track <= 0.0) {
    return result;
  }

  // The thumb is as long as the visible fraction of everything there is to
  // scroll through, with a floor. A very long list would otherwise end up with
  // a one-pixel thumb.
  const double scrollable = range.length() + containerLength;
  const double fraction = containerLength / scrollable;
  const double length = std::min(track, std::max(kScrollIndicatorMinimumLength, track * fraction));

  const double progress = std::clamp((scrollOffset - range.minimum) / range.length(), 0.0, 1.0);

  result.visible = true;
  result.length = length;
  result.offset = start + progress * (track - length);
  return result;
}

} // namespace basalt
