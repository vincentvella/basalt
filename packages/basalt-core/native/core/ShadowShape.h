// The shape a box shadow is cast by: a corner radius grown by the spread.
//
// Not an addition, which is the whole reason this is written down. The CSS spec
// gives a curve: a corner tighter than the spread is rounded off more gently
// than the spread alone would make it, so a small radius on a widely spread
// shadow does not turn into a circle.
//
// https://drafts.csswg.org/css-backgrounds/#shadow-shape
//
// Shared because two hosts compute it and the third does not: GSK's shadow
// nodes take the view's own outline and grow it themselves, where AppKit builds
// a `CGPath` and Direct2D a geometry, and both of those have to do this. It is
// a port of React Native's iOS half, which took it from the same place; a
// second hand-rolled copy of a cubic is exactly the kind of thing that ends up
// subtly different on one platform.
//
// A negative spread is allowed and shrinks the box, which is what an inset
// shadow's hole is built from, so `std::abs` is in the ratio rather than a
// guard against it.

#pragma once

#include <cmath>

namespace basalt {

// `radius` grown by `spread`, never below zero.
inline double spreadRadius(double radius, double spread) {
  double adjustment = spread;
  if (radius < std::abs(spread)) {
    const double ratio = radius / std::abs(spread);
    adjustment *= 1.0 + std::pow(ratio - 1.0, 3.0);
  }
  const double grown = radius + adjustment;
  return grown > 0.0 ? grown : 0.0;
}

} // namespace basalt
