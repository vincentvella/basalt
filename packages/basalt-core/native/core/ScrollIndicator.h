// The scrollbar a scroll view draws over itself.
//
// None of the three hosts drew one, so a long list gave no sign of how long it
// was or where you were in it -- which on a desktop reads as a bug rather than
// a style.
//
// ## Drawn rather than borrowed
//
// Each toolkit has a scrollbar and none of them is available here. GTK's comes
// with `GtkScrolledWindow`, AppKit's `NSScroller` comes with `NSScrollView`,
// and this platform's scroll view is neither -- it is a `<View>` that moves its
// children, because that is what Fabric hands it. Adopting a toolkit scroll
// container would mean giving it the scrolling too, and then React Native and
// the toolkit would both believe they own the offset.
//
// So it is drawn, and the geometry is decided here so that all three draw the
// same thing. That also makes it testable, which is worth more than usual: a
// scrollbar is pure paint, and paint is what these hosts cannot assert.
//
// ## The shape
//
// An overlay indicator, like every modern desktop: a rounded bar inset from the
// trailing edge, sized to the fraction of the content on screen, positioned by
// how far through it you are. Not a classic scrollbar with arrows and a
// trough -- nothing here is a drag target yet, which `docs/backlog` records.

#pragma once

#include "ScrollBounds.h"

namespace basalt {

// Drawing constants, here rather than in three hosts so they cannot drift.
// Thin, because it sits over content rather than beside it.
inline constexpr double kScrollIndicatorThickness = 6.0;
// From the trailing edge, and from each end of the track.
inline constexpr double kScrollIndicatorInset = 2.0;
// Below this a thumb stops reading as a position and starts reading as a dot.
inline constexpr double kScrollIndicatorMinimumLength = 24.0;

// `indicatorStyle`, as React Native's three values.
//
// Two of them draw the same thing here, and that is iOS's doing rather than a
// shortcut: `UIScrollViewIndicatorStyleDefault` is "black with a white border"
// and `.black` is black alone, so the difference is a border these hosts do not
// draw. `.white` is the one that matters -- it is what a list over dark content
// asks for, and a black thumb there is invisible. backlog/scrollview.md records
// the border, which is why the support row is `partial` rather than done.
enum class ScrollIndicatorStyle {
  Default,
  Black,
  White,
};

// Straight sRGB with alpha, which all three toolkits take in that form.
struct ScrollIndicatorColour {
  float red{0.0F};
  float green{0.0F};
  float blue{0.0F};
  float alpha{0.0F};
};

// Translucent, so the thumb reads over whatever it sits on rather than hiding
// it. One number for both colours: a white thumb that was more opaque than the
// black one would read as a different control.
inline constexpr float kScrollIndicatorAlpha = 0.35F;

// `inline` rather than a function in the .cpp, and not for speed: the three
// view layers link no core at all -- each is built and tested with nothing but
// its toolkit, which is what lets the Win32 suite run on a box with a compiler
// and the SDK -- so anything they take from here has to be a header. The view
// layers need this one to spell their own default thumb, and the managers need
// it to answer the prop.
inline ScrollIndicatorColour scrollIndicatorColourFor(ScrollIndicatorStyle style) {
  if (style == ScrollIndicatorStyle::White) {
    return ScrollIndicatorColour{
        .red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = kScrollIndicatorAlpha};
  }
  // `Default` and `Black` both: see above for why those two are one answer
  // here.
  return ScrollIndicatorColour{
      .red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = kScrollIndicatorAlpha};
}

struct ScrollIndicator {
  // False when everything fits: a scrollbar for content that cannot scroll is
  // noise, and every list shorter than its container would grow one.
  bool visible{false};
  // Along the track, from its start, in points.
  double offset{0.0};
  double length{0.0};
};

// Where the thumb goes for one axis.
//
// `scrollOffset` may be outside the range -- an elastic overscroll goes
// negative at the top -- and the result is clamped rather than refused, because
// a thumb that vanished at the limits would flicker at exactly the moment
// somebody is looking at it.
//
// Two sets of insets, and they do different jobs. `content` is the app's
// `contentInset`: it changes how far the view scrolls, so it changes what
// fraction of the way through any given offset is. `indicator` is
// `scrollIndicatorInsets`: it shortens the track the thumb runs in and nothing
// else, which is what a header overlaying the top of a list wants -- the bar
// should start below it.
ScrollIndicator scrollIndicatorFor(double containerLength,
                                   double contentLength,
                                   double scrollOffset,
                                   ScrollAxisInsets content = {},
                                   ScrollAxisInsets indicator = {});

} // namespace basalt
