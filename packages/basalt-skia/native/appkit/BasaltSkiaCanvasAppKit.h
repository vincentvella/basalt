// What the AppKit canvas exposes beyond `AppKitSkiaPeer.h`'s two functions:
// the arithmetic it does, and what it attached.
//
// Both exist because of what a test can and cannot see. The mounting manager
// needs only `applySkiaCanvas` and `forgetSkiaCanvas`, and everything else
// about a canvas is observable through the view it was given -- except two
// things:
//
//   - **The pixel size.** `RNSkWindowSurface::attach` takes pixels where 2.x
//     took points, and on a machine whose scale is 1 the two are the same
//     number, so a test cannot tell a peer that multiplied from one that
//     forgot to. A test has no way to make the scale 2 either: it comes from
//     `currentDeviceMetrics()`, which answers from what a host reported and
//     there is no host in a test. So the multiplication is a function, called
//     with the density the test chooses.
//   - **The size the surface is attached at.** `CAMetalLayer` ignores a
//     `drawableSize` of zero and keeps the last one, so the layer cannot say
//     whether a zero-sized layout was attached -- which is exactly the case
//     the peer guards against.

#pragma once

#include <react/renderer/core/ReactPrimitives.h>

#include <optional>
#include <utility>

namespace basalt {

// The pixel size a layout of this many points has at this density, which is
// what the surface is configured with.
//
// Rounded rather than truncated, which is what upstream's own SkiaView.mm does
// and for the reason it gives: a product like 1169.9999 should give the pixel
// size the layout means. Zero or negative for a layout that has none -- Yoga
// hands those out before the first real layout -- and the caller attaches
// nothing then.
std::pair<int, int> skiaCanvasPixelSize(double widthInPoints,
                                        double heightInPoints,
                                        double density);

// The size in pixels the canvas for `tag` has its surface attached at, or
// nothing when there is no canvas or it has no surface yet. For tests: what
// the swapchain was configured with is not readable off the layer.
std::optional<std::pair<int, int>> skiaCanvasAttachedSize(facebook::react::Tag tag);

} // namespace basalt
