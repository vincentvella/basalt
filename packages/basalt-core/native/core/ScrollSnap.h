// Where a scroll should come to rest.
//
// `pagingEnabled`, `snapToInterval` and `snapToOffsets` are how a carousel, a
// paged list and a stop-at-each-card list are written in React Native, and none
// of them did anything here: a drag ended wherever the fingers left it.
//
// ## Why this is arithmetic and not a host's business
//
// Choosing the resting point needs the offset, the velocity, the container, the
// content and the props -- and nothing from a toolkit. So it is decided here,
// once, and each host does the same two things with the answer: stop the fling
// and animate to it, which is what `core/ScrollAnimation.h` is for.
//
// That also makes it testable. Almost nothing about a scroll view on these
// hosts can be asserted without a display; this can.
//
// ## The rules
//
// A flick settles on the next point in the direction it was flicked, even when
// the finger left nearer the point behind. That is what makes a carousel feel
// like a carousel rather than a list that sometimes changes page -- and it is
// why velocity is a parameter rather than the caller simply passing the offset
// it landed on.
//
// "Next in that direction" rather than "a whole page on": twenty pixels into a
// page, flicked back, the answer is that page's start and not the one before
// it. The rule is symmetric -- forwards from the same place is the page ahead --
// and a small flick must not be able to travel further than a large one.
//
// Below that threshold the nearest point wins, which is the behaviour of
// letting go without meaning anything by it.

#pragma once

#include <optional>
#include <vector>

namespace basalt {

// Where in the container a snap point should line up, as React Native's
// `snapToAlignment` names it.
enum class ScrollSnapAlignment {
  Start,
  Center,
  End,
};

struct ScrollSnapConfig {
  // `pagingEnabled`: the interval is the container's own length, so a page is a
  // screenful. Takes precedence over the two below, as it does on iOS.
  bool paging{false};
  // `snapToInterval`, in points. Zero means unused.
  double interval{0.0};
  // `snapToOffsets`. Empty means unused; when set it wins over `interval`,
  // because a list of explicit points is a more specific statement than a
  // spacing.
  std::vector<double> offsets;
  ScrollSnapAlignment alignment{ScrollSnapAlignment::Start};

  bool enabled() const {
    return paging || interval > 0.0 || !offsets.empty();
  }
};

// Pixels per second past which a gesture counts as a flick rather than a
// release. Low enough that a deliberate flick always carries, high enough that
// the wobble of letting go does not.
inline constexpr double kScrollSnapFlickVelocity = 250.0;

// The offset to settle at, or nothing when no snapping is configured.
//
// `velocity` is in pixels per second, positive meaning the content offset is
// growing -- the same sign convention as everything else here.
std::optional<double> scrollSnapTarget(const ScrollSnapConfig &config,
                                       double offset,
                                       double velocity,
                                       double containerLength,
                                       double contentLength);

} // namespace basalt
