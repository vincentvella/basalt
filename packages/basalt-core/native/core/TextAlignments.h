// `textAlign`, resolved to an edge.
//
// React Native 0.87 added `start` and `end` beside `left` and `right`, and the
// two kinds are not the same question. `left` is the left of the box. `end` is
// the edge a line of text *finishes* at, which in a right-to-left paragraph is
// the left one -- so a paragraph with `writingDirection: 'rtl'` and
// `textAlign: 'end'` is flush left, and all three hosts had it flush right.
//
// `natural`, which is the default, is the other relative one: the edge a line
// starts from.
//
// ## Why a table in core rather than three enum mappings
//
// Because it was three enum mappings, and the three disagreed. Measured on
// 2026-10-10, over a paragraph of Hebrew in a 300pt box, before this existed:
//
//   | the app wrote           | GTK   | AppKit | Win32 |
//   | rtl + end               | right | right  | left  |
//   | rtl + left              | left  | right  | left  |
//   | hebrew + natural        | right | left   | left  |
//   | hebrew + left           | right | left   | left  |
//
// Four rows, no two hosts agreeing on all of them, and one correct cell between
// them -- Win32's `end`, which is right because DirectWrite's alignments are
// themselves relative. Each host had made a local decision about one enum
// value, and the question is not local: it is "which edge, given a direction",
// asked three times.
//
// So the question is answered here, and what is left per host is the spelling:
// a `PangoAlignment`, a Core Text flush factor, a `DWRITE_TEXT_ALIGNMENT`.
// `core/TextDirection.h` answers the direction half.

#pragma once

#include <react/renderer/attributedstring/AttributedString.h>
#include <react/renderer/attributedstring/TextAttributes.h>

#include <optional>

namespace basalt {

// Where the text sits in the box, with nothing relative left in it.
//
// `Justified` is here because React Native's enum has it, and it is not an edge
// at all: every host passes it straight to the engine, which stretches the
// lines and then puts the last one against the start edge. Kept in the enum so
// that a caller switching on this handles it rather than falling into a default
// that silently means left.
enum class PhysicalTextAlignment { Left, Center, Right, Justified };

// The edge for `alignment` in a paragraph running `rightToLeft`.
//
// No alignment at all is `natural`, which is React Native's own default and the
// reason this takes an optional: a paragraph that said nothing still has an
// answer, and in a right-to-left one it is not the left edge.
inline PhysicalTextAlignment physicalTextAlignment(
    std::optional<facebook::react::TextAlignment> alignment,
    bool rightToLeft) {
  if (!alignment.has_value()) {
    return rightToLeft ? PhysicalTextAlignment::Right : PhysicalTextAlignment::Left;
  }

  switch (*alignment) {
    case facebook::react::TextAlignment::Left:
      return PhysicalTextAlignment::Left;
    case facebook::react::TextAlignment::Right:
      return PhysicalTextAlignment::Right;
    case facebook::react::TextAlignment::Center:
      return PhysicalTextAlignment::Center;
    case facebook::react::TextAlignment::Justified:
      return PhysicalTextAlignment::Justified;
#if BASALT_RN_MINOR >= 87
    // The two that arrived in 0.87, and the whole reason this file exists.
    case facebook::react::TextAlignment::Start:
      return rightToLeft ? PhysicalTextAlignment::Right : PhysicalTextAlignment::Left;
    case facebook::react::TextAlignment::End:
      return rightToLeft ? PhysicalTextAlignment::Left : PhysicalTextAlignment::Right;
#endif
    case facebook::react::TextAlignment::Natural:
      break;
  }
  return rightToLeft ? PhysicalTextAlignment::Right : PhysicalTextAlignment::Left;
}

// The edge's name, for the tree dump the three hosts compare line by line.
//
// The resolved edge rather than the prop, which is the point: `textAlign: 'end'`
// is the same word on every host and a different edge depending on the
// paragraph's direction, so printing the prop would compare the question rather
// than the answer. Nothing else in a dump shows it -- a right-aligned paragraph
// has the same box and the same string as a left-aligned one.
inline const char *physicalTextAlignmentName(PhysicalTextAlignment alignment) {
  switch (alignment) {
    case PhysicalTextAlignment::Center:
      return "center";
    case PhysicalTextAlignment::Right:
      return "right";
    case PhysicalTextAlignment::Justified:
      return "justified";
    case PhysicalTextAlignment::Left:
      break;
  }
  return "left";
}

// The name for a paragraph, which is the question a mounting manager has: it
// holds the attributed string and wants the line to print.
const char *paragraphTextAlignmentName(const facebook::react::AttributedString &attributedString);

// How far along the line's own free space the text sits: 0 for the left edge,
// 0.5 for the middle, 1 for the right.
//
// Core Text's `CTLineGetPenOffsetForFlush` takes exactly this, and it is what
// the AppKit layer draws with -- it lays out its own lines so that
// `numberOfLines` can be honoured, which means the frame never applies the
// alignment and the offset has to be computed. Justified text is flush with the
// start edge, which is where its last line goes.
inline double textFlushFactor(PhysicalTextAlignment alignment, bool rightToLeft) {
  switch (alignment) {
    case PhysicalTextAlignment::Center:
      return 0.5;
    case PhysicalTextAlignment::Right:
      return 1.0;
    case PhysicalTextAlignment::Justified:
      return rightToLeft ? 1.0 : 0.0;
    case PhysicalTextAlignment::Left:
      break;
  }
  return 0.0;
}

} // namespace basalt
