// How far a scroll view may be scrolled, once `contentInset` has had its say.
//
// Three hosts had the same four lines of arithmetic -- clamp between zero and
// `content - container` -- and none of them read `contentInset`, which was
// parsed, stored, echoed back in the scroll event, and otherwise ignored. A
// list that asked to be inset from the top scrolled as though it had not.
//
// ## What an inset does
//
// It is not padding. Padding makes the content bigger; an inset makes the
// *scrollable range* bigger while leaving the content the size it is. With a
// top inset of 50 the list may be pulled 50 points further down than its first
// row, exposing empty space above it -- which is what a header that overlays
// the list needs, and the reason iOS has the prop at all.
//
// So the range runs from `-leading` to `content - container + trailing`,
// instead of from zero to `content - container`. With no insets those are the
// same numbers, which is why nothing changed for anybody who never set one.
//
// ## Two insets, not one
//
// `contentInset` moves the content and `scrollIndicatorInsets` moves the
// scrollbar, and an app may set either without the other: a header that covers
// the top of the list usually wants both, and a window with a rounded corner
// wants only the second. They are separate parameters here for the same reason
// React Native keeps them separate props.

#pragma once

namespace basalt {

// One axis of an `EdgeInsets`: the inset before the content and the one after
// it. Vertically that is top and bottom; horizontally, left and right.
struct ScrollAxisInsets {
  double leading{0.0};
  double trailing{0.0};
};

// The offsets a scroll view may rest at, on one axis. `minimum` is zero or
// negative, `maximum` is at least `minimum`.
struct ScrollRange {
  double minimum{0.0};
  double maximum{0.0};

  double length() const { return maximum - minimum; }
};

// The range for one axis.
//
// `maximum` is never below `minimum`: content shorter than its container with a
// leading inset can still be pulled, and the two ends must not cross, or a
// clamp between them would answer with whichever was written last.
ScrollRange scrollRangeFor(double containerLength,
                           double contentLength,
                           ScrollAxisInsets insets = {});

// An offset brought inside that range.
//
// The one place the three hosts agreed already, and now the one place it is
// written: an offset outside the range is a momentum frame, a `scrollTo` past
// the end, or content that shrank under a scrolled view, and all three want the
// nearest reachable position rather than a refusal.
double clampScrollOffset(double offset,
                         double containerLength,
                         double contentLength,
                         ScrollAxisInsets insets = {});

} // namespace basalt
