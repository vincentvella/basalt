// Where a paragraph sits in a box taller than it is: the arithmetic half.
//
// Separate from `core/TextAlignments.h` for one reason, and it is the reason
// `FocusRing.h` and `ControlMetrics.h` are separate too: this is needed by each
// host's *widget* layer -- `RnView.cpp`, `RnTextLayout.mm`,
// `RnWin32TextLayout.cpp` -- and that layer has no React Native in it and
// cannot include a header that names `TextAlignmentVertical`. Each of those
// three draws the paragraph, and the box it is drawing into is only known then:
// a view resized without its props changing has new slack and the same text.
//
// So the enum lives in TextAlignments.h with the rest of the alignment
// decisions, and the one line all three draw with lives here.

#pragma once

namespace basalt {

// How far down to start drawing, given the box and what the text needs.
//
// Never negative: a paragraph taller than its box starts at the top, however it
// was aligned, because the alternative is text pushed out through the top edge
// where there is nothing to scroll it back. iOS and Android both do this, and a
// `numberOfLines` paragraph is the case -- the box is the lines that fit and the
// text is all of them.
inline double textVerticalOffset(double boxHeight, double textHeight, double flush) {
  const double slack = boxHeight - textHeight;
  if (slack <= 0.0 || flush <= 0.0) {
    return 0.0;
  }
  return slack * flush;
}

} // namespace basalt
