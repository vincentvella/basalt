// React DevTools' element highlighter.
//
// `DebuggingOverlay` is the view React Native mounts over an app when DevTools
// asks it to draw attention to something. Two things use it, and they look
// different on purpose:
//
//   highlightTraceUpdates   "Highlight updates when components render" -- a
//                           coloured outline around everything that just
//                           re-rendered, in the colour DevTools chose for how
//                           often it has. It is meant to flash, so it clears
//                           itself; nothing tells it to.
//
//   highlightElements       The blue box over the component being inspected,
//                           which stays until `clearElementsHighlights`.
//
// The rectangles arrive as command arguments rather than as props, which is why
// this is a parser rather than another prop reader: a command's payload is
// `folly::dynamic` and every host would otherwise walk it itself.
//
// The drawing is not here. A rectangle with a colour is the same idea on three
// desktops and three snapshot routines are not, so each view layer draws these
// the way it draws everything else.

#pragma once

#include "ControlMetrics.h"

#include <folly/dynamic.h>

#include <vector>

namespace basalt {

struct Highlight {
  float x{0.0F};
  float y{0.0F};
  float width{0.0F};
  float height{0.0F};
  // Premultiplied-free 0..1, as every colour in this project's view layers is.
  float color[4]{0.0F, 0.0F, 0.0F, 0.0F};
  // Whether to fill as well as outline. An inspected element is filled and a
  // trace update is not, which is what tells them apart at a glance.
  bool filled{false};
};

// `highlightTraceUpdates(updates)`: `[{id, rectangle: {x, y, width, height},
// color}]`, where the colour is a processed one -- an integer, not a string.
//
// An update with no colour keeps DevTools' own default rather than being
// dropped: the rectangle is the information, and the colour is how often it
// has re-rendered.
std::vector<Highlight> parseTraceUpdates(const folly::dynamic &args);

// `highlightElements(elements)`: `[{x, y, width, height}]`, with no colour --
// DevTools' blue is the platform's to choose, and all three choose the same one
// so that a screenshot of an inspected element is the same picture everywhere.
std::vector<Highlight> parseElementHighlights(const folly::dynamic &args);

// How long a trace update stays up. React Native's own overlay clears them for
// itself rather than waiting to be told, because the point is that they flash:
// a box left behind after a component stopped re-rendering says the opposite of
// what it means.
inline constexpr double kTraceUpdateLifetimeMs = 1500.0;

} // namespace basalt
