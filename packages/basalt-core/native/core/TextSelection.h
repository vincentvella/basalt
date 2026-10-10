// Selecting text with a pointer, which is a desktop expectation and was a
// `<Text selectable>` that did nothing.
//
// `BaseParagraphProps::isSelectable` is the field, and both `selectable` and
// `userSelect` arrive in it -- React Native's own `Text.js` maps the style onto
// the prop. What a host does with it is three questions, and only the third is
// shared: where a point lands in the text, what rectangles a range covers, and
// *what a press and a drag mean*. The first two are each engine's own
// (`pango_layout_xy_to_index`, `CTLineGetStringIndexForPosition`,
// `IDWriteTextLayout::HitTestPoint`); the third is this file, because it is a
// state machine with edges, and three hosts guessing at the edges separately is
// how they would come to disagree about what a click does.
//
// ## The hard part is arbitration, not the selection
//
// A press inside selectable text cannot immediately become a selection, because
// a press is also how a `<Pressable>` wrapping that text is pressed, and how a
// `<ScrollView>` containing it is dragged. Claiming on the press would break
// both: a tap on a selectable label inside a button would select a word and
// never fire `onPress`.
//
// So a press only *remembers* where it landed. The gesture becomes a selection
// when the pointer has moved far enough to say the person is sweeping rather
// than tapping, and at that moment the host cancels the touch sequence it had
// been reporting -- which is what the responder system does on a phone when a
// scroll claims a touch, and reads the same way to React: a touchstart, some
// moves, a cancel.
//
// A press that never moves is a tap, and a tap clears whatever was selected,
// which is what every desktop does.
//
// ## Indices are the host's own unit
//
// Pango counts UTF-8 bytes, Core Text and DirectWrite count UTF-16 code units,
// and nothing here needs them to agree: a paragraph's selection never leaves
// the host. There is no `onSelectionChange` for a `<Text>` -- the only event
// `ParagraphEventEmitter` has is `onTextLayout` -- so the indices are between a
// hit test and a highlight, both of which are the same engine's. What crosses
// to the clipboard is the substring, not an offset.
//
// So this file does no arithmetic on an index beyond comparing and ordering
// them, and says so here rather than inviting somebody to add a conversion.

// What a selection *looks* like is core/TextHighlight.h, which is a colour and
// no React Native: each host's widget layer paints it, and that layer cannot
// include this file. Not included from here, so the dependency stays where it
// is used.

#pragma once

#include <react/renderer/graphics/Point.h>
#include <react/renderer/core/ReactPrimitives.h>

#include <functional>
#include <string>

namespace basalt {

// How far a pointer has to move before a press becomes a selection rather than
// a tap, in points.
//
// Two, which is the smallest number that is not a shaky hand: a mouse delivers
// sub-pixel motion on a trackpad, and a threshold of zero would turn every
// click on selectable text into a selection and cancel every tap. React
// Native's own `Pressability` allows a touch to wander much further before it
// stops being a press, which is the right number for a finger and the wrong one
// for a cursor.
inline constexpr double kTextSelectionThreshold = 2.0;

// An ordered range, which is what a highlight and a copy both want. `length` is
// zero when nothing is selected.
struct TextSelectionRange {
  int start{0};
  int length{0};

  bool empty() const {
    return length <= 0;
  }
};

// Where a selection started and where the pointer is now, for one paragraph.
//
// One per surface rather than one per paragraph: selecting inside a second
// `<Text>` clears the first, which is what a desktop does and what a single
// anchor enforces by construction.
class TextSelection {
 public:
  // A press at `index` inside the selectable paragraph `tag`, at `at` in the
  // surface's coordinates. Nothing is selected yet; see the header.
  void press(facebook::react::Tag tag, int index, facebook::react::Point at);

  // The pointer has moved to `index` at `at`. Answers true the once, when the
  // move claims the gesture: the caller cancels the touch sequence it was
  // reporting and stops reporting moves.
  //
  // False, and no selection, until the pointer has travelled
  // `kTextSelectionThreshold` from the press. False forever if the press was
  // not inside selectable text.
  bool moveTo(int index, facebook::react::Point at);

  // The pointer is up. The selection stays on screen; what it stops is the
  // dragging.
  void release();

  // Nothing is selected. A tap, a press in a different paragraph, a mutation
  // that replaced the text: all of them end here.
  void clear();

  // Whether a drag is currently selecting rather than touching. The caller
  // reports no touch moves while this is true.
  bool dragging() const {
    return claimed_ && pressed_;
  }

  // Whether there is a selection to draw, which outlives the drag.
  bool active() const {
    return claimed_ && !range().empty();
  }

  facebook::react::Tag tag() const {
    return claimed_ ? tag_ : 0;
  }

  // The selected range, ordered: a drag to the left of its anchor selects the
  // same text as a drag to the right.
  TextSelectionRange range() const;

 private:
  facebook::react::Tag tag_{0};
  int anchor_{0};
  int focus_{0};
  facebook::react::Point origin_{};
  bool pressed_{false};
  bool claimed_{false};
};

// --- Copying it ---------------------------------------------------------------
//
// The two places a copy is asked for are both outside the pointer path: a
// `Copy` role in a menu, which `performMenuRole` performs, and Ctrl or Cmd with
// C, which arrives at a window's key controller. Neither can reach the object
// holding the selection -- one is in the platform services and the other in the
// host's own main -- so the host leaves a way to ask here, which is the
// arrangement `setWindowBoundsListener` and `setEventListenerInstaller` use for
// the same reason.
//
// Set by whatever owns the selection: GtkTouchDispatcher, the AppKit mounting
// manager, the Win32 window. Null when no host has set one, which is every
// build that is not a host -- the tests and the portability probe.
void setSelectedTextProvider(std::function<std::string()> provider);

// What is selected, or empty when nothing is. Safe to call with no provider.
std::string selectedText();

// Puts it on the clipboard and says whether there was anything to put. False is
// the answer a `Copy` role wants when no paragraph has a selection: it means
// "ask the focused widget instead", which is what the role meant before
// paragraphs could be selected at all.
bool copySelectedText();

} // namespace basalt
