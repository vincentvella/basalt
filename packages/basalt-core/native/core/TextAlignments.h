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

#include "TextVerticalAlign.h"

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

// --- The other axis ----------------------------------------------------------
//
// `textAlignVertical` puts the paragraph somewhere in its own box: `top`,
// `bottom`, `center` or `auto`. `verticalAlign` is the cross-platform spelling
// of the same thing and needs nothing of its own here -- React Native's
// `Text.js` and `TextInput.js` both rewrite it into `textAlignVertical` before
// the props are sent, with `middle` becoming `center`. Measured on 2026-10-10;
// the backlog said the cross-platform name was dropped before any platform saw
// it, which was true of ReactCommon and not of the JavaScript above it.
//
// `auto` is the top, which is what every engine does with no instruction and
// what React Native's own default produces.
//
// Only reachable when the box is taller than the text, which for a paragraph
// means a `<Text>` with a height of its own: a paragraph that Yoga sized to its
// own content has nothing to be aligned inside.

// What the app asked for, for the tree dump: "auto", "top", "center",
// "bottom", or nullptr when it said nothing.
//
// The prop rather than the offset it produces, which is the opposite of the
// horizontal line above and for a reason: the edge a line sits against is the
// same word on every host and a different edge depending on the direction, so
// printing the prop there would compare the question. Here the prop *is* the
// answer, and the offset depends on the box, which differs between hosts by a
// line's worth of font metrics. What this line proves is the whole chain from
// JavaScript -- `verticalAlign` is rewritten into `textAlignVertical` by React
// Native's own `Text.js`, and nothing else here can show that it arrived.
inline const char *textAlignVerticalName(
    std::optional<facebook::react::TextAlignmentVertical> alignment) {
  if (!alignment.has_value()) {
    return nullptr;
  }
  switch (*alignment) {
    case facebook::react::TextAlignmentVertical::Auto:
      return "auto";
    case facebook::react::TextAlignmentVertical::Top:
      return "top";
    case facebook::react::TextAlignmentVertical::Center:
      return "center";
    case facebook::react::TextAlignmentVertical::Bottom:
      return "bottom";
  }
  return nullptr;
}

// How far down its box the paragraph sits: 0 for the top, 0.5 for the middle,
// 1 for the bottom. The same shape as the horizontal flush factor above, and
// for the same reason -- it is the one number each of the three hosts can use,
// over a Pango translation, a Core Text origin and a DirectWrite draw origin.
inline double textVerticalFlushFactor(
    std::optional<facebook::react::TextAlignmentVertical> alignment) {
  if (!alignment.has_value()) {
    return 0.0;
  }
  switch (*alignment) {
    case facebook::react::TextAlignmentVertical::Center:
      return 0.5;
    case facebook::react::TextAlignmentVertical::Bottom:
      return 1.0;
    case facebook::react::TextAlignmentVertical::Top:
    case facebook::react::TextAlignmentVertical::Auto:
      break;
  }
  return 0.0;
}

// The offset that factor produces, given a box and the text in it, is
// `textVerticalOffset` in core/TextVerticalAlign.h -- a separate header because
// each host's widget layer needs it and has no React Native in it. The box is
// only known while drawing, so that is where it is asked.

} // namespace basalt
