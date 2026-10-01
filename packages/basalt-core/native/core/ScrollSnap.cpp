#include "ScrollSnap.h"

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
std::optional<double> nearestOffset(const std::vector<double> &offsets,
                                    double offset,
                                    double velocity) {
  if (offsets.empty()) {
    return std::nullopt;
  }
  std::vector<double> sorted = offsets;
  std::sort(sorted.begin(), sorted.end());

  auto nearest = sorted.begin();
  double best = std::abs(sorted.front() - offset);
  for (auto it = sorted.begin(); it != sorted.end(); ++it) {
    const double distance = std::abs(*it - offset);
    if (distance < best) {
      best = distance;
      nearest = it;
    }
  }

  if (velocity > kScrollSnapFlickVelocity) {
    // The first entry strictly past where we are. A flick forwards must not
    // settle back on the point it started from.
    for (auto it = sorted.begin(); it != sorted.end(); ++it) {
      if (*it > offset) {
        return *it;
      }
    }
    return sorted.back();
  }
  if (velocity < -kScrollSnapFlickVelocity) {
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
      if (*it < offset) {
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

  // An explicit list wins over a spacing, and a spacing over paging only when
  // paging is off: `pagingEnabled` is the coarsest statement and iOS lets it
  // take precedence.
  if (!config.paging && !config.offsets.empty()) {
    const auto target = nearestOffset(config.offsets, offset, velocity);
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
  const double aligned = offset + shift;

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
