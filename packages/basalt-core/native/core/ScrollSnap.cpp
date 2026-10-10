#include "ScrollSnap.h"

#include "ScrollMomentum.h"

#include <algorithm>
#include <cmath>

namespace basalt {

namespace {

// What the alignment shifts by: a centred snap lines the middle of the
// container up with a point, an end-aligned one lines up its trailing edge.
double alignmentShift(ScrollSnapAlignment alignment, double containerLength) {
  switch (alignment) {
    case ScrollSnapAlignment::Start:
      return 0.0;
    case ScrollSnapAlignment::Center:
      return containerLength / 2.0;
    case ScrollSnapAlignment::End:
      return containerLength;
  }
  return 0.0;
}

double clampToContent(double offset, double containerLength, double contentLength) {
  const double furthest = std::max(0.0, contentLength - containerLength);
  return std::clamp(offset, 0.0, furthest);
}

// The nearest of an explicit list, with a flick moving at least one entry.
//
// `landing` is where the fling would have come to rest and `offset` is where
// the finger left; for a release the two are the same. The candidates are the
// listed offsets *and* the content's own two edges, which is what
// `snapToStart` and `snapToEnd` are about -- "by default the beginning of the
// list counts as a snap offset", in upstream's words, and those two props turn
// the default off.
std::optional<double> nearestOffset(const ScrollSnapConfig &config,
                                    double offset,
                                    double landing,
                                    double velocity,
                                    double furthest) {
  if (config.offsets.empty()) {
    return std::nullopt;
  }
  std::vector<double> sorted = config.offsets;
  std::sort(sorted.begin(), sorted.end());
  const double first = sorted.front();
  const double last = sorted.back();

  // Free scrolling between an edge and the listed point nearest it, when the
  // app asked for that. Upstream's own two conditions, and both halves matter:
  // a list already past the last point coasts, and one that is *not* yet past
  // it still settles there rather than sailing into the gap.
  if (!config.snapToEnd && landing >= last) {
    if (offset >= last) {
      return std::nullopt;
    }
    return last;
  }
  if (!config.snapToStart && landing <= first) {
    if (offset <= first) {
      return std::nullopt;
    }
    return first;
  }

  // The content's edges as candidates, which is the default behaviour the two
  // props above switch off. Inserted rather than special-cased so that
  // "nearest" and "next in that direction" both see them.
  sorted.insert(sorted.begin(), 0.0);
  sorted.push_back(furthest);
  std::sort(sorted.begin(), sorted.end());

  auto nearest = sorted.begin();
  double best = std::abs(sorted.front() - landing);
  for (auto it = sorted.begin(); it != sorted.end(); ++it) {
    const double distance = std::abs(*it - landing);
    if (distance < best) {
      best = distance;
      nearest = it;
    }
  }

  if (velocity > kScrollSnapFlickVelocity) {
    // The first entry at or past where the fling would have landed. A flick
    // forwards must not settle back on the point it started from, which is
    // what the `> offset` rather than `>= landing` guard is for when the two
    // are the same.
    for (auto it = sorted.begin(); it != sorted.end(); ++it) {
      if (*it >= landing && *it > offset) {
        return *it;
      }
    }
    return sorted.back();
  }
  if (velocity < -kScrollSnapFlickVelocity) {
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
      if (*it <= landing && *it < offset) {
        return *it;
      }
    }
    return sorted.front();
  }
  return *nearest;
}

} // namespace

std::optional<double> scrollSnapTarget(const ScrollSnapConfig &config,
                                       double offset,
                                       double velocity,
                                       double containerLength,
                                       double contentLength) {
  if (!config.enabled()) {
    return std::nullopt;
  }

  // Where the fling would have come to rest, which is the offset a snap point
  // is chosen next to unless the app asked for the paged behaviour. Zero for a
  // release, for `pagingEnabled` -- one page per flick, whatever it was worth --
  // and for `disableIntervalMomentum`, which is the prop for asking that a
  // spacing behave like a page.
  const double landing = config.paging || config.disableIntervalMomentum
      ? offset
      : offset + scrollMomentumDistance(velocity, config.decelerationRate);

  // An explicit list wins over a spacing, and a spacing over paging only when
  // paging is off: `pagingEnabled` is the coarsest statement and iOS lets it
  // take precedence.
  if (!config.paging && !config.offsets.empty()) {
    const auto target = nearestOffset(
        config, offset, landing, velocity, std::max(0.0, contentLength - containerLength));
    if (!target) {
      return std::nullopt;
    }
    return clampToContent(*target, containerLength, contentLength);
  }

  const double interval = config.paging ? containerLength : config.interval;
  if (interval <= 0.0) {
    return std::nullopt;
  }

  // Aligned snapping is the same arithmetic about a shifted point: what lines
  // up with a multiple is the container's leading edge, its middle, or its
  // trailing edge.
  const double shift = alignmentShift(config.alignment, containerLength);
  const double aligned = landing + shift;

  double index = std::floor(aligned / interval);
  const double within = aligned - index * interval;

  if (velocity > kScrollSnapFlickVelocity) {
    index += 1.0;
  } else if (velocity < -kScrollSnapFlickVelocity) {
    // Already past a boundary by less than half means the flick backwards is
    // to the boundary itself rather than the one before it.
    if (within > 0.0) {
      // index already floors to the boundary behind us.
    } else {
      index -= 1.0;
    }
  } else if (within > interval / 2.0) {
    index += 1.0;
  }

  return clampToContent(index * interval - shift, containerLength, contentLength);
}

} // namespace basalt
