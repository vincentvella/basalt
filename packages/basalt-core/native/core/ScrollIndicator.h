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
// `UIScrollViewIndicatorStyleDefault` is "black with a white border", `.black`
// is black alone, and `.white` is the one a list over dark content asks for,
// where a black thumb is invisible. So the three are two fills and one border,
// and the border is what tells `default` from `black`: it is a hairline of
// white around the pill, which is what keeps the default thumb visible over
// dark and light content both.
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
  // `Default` and `Black` share a fill and differ by the border below, which
  // is exactly how iOS's two differ.
  return ScrollIndicatorColour{
      .red = 0.0F, .green = 0.0F, .blue = 0.0F, .alpha = kScrollIndicatorAlpha};
}

// A hairline, which is what a border around a six-point pill can be without
// becoming the thumb. One point rather than a fraction: two of the three hosts
// stroke in logical points and a half-point line lands differently in each.
inline constexpr double kScrollIndicatorBorderWidth = 1.0;

// Rather more opaque than the fill, for the one job it has: the fill is a wash
// that lets the content through, and an outline as faint as that would not
// separate a black thumb from black content behind it.
inline constexpr float kScrollIndicatorBorderAlpha = 0.6F;

// The border around the thumb, or nothing for the two styles that have none.
//
// Only `Default` has one. `black` and `white` are the styles an app chooses
// *because* it knows what is behind the thumb, and iOS draws neither with an
// outline; adding one to either would be this platform inventing a look.
inline ScrollIndicatorColour scrollIndicatorBorderColourFor(ScrollIndicatorStyle style) {
  if (style == ScrollIndicatorStyle::Default) {
    return ScrollIndicatorColour{
        .red = 1.0F, .green = 1.0F, .blue = 1.0F, .alpha = kScrollIndicatorBorderAlpha};
  }
  return ScrollIndicatorColour{};
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
