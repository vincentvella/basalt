// Which view claims which key, and what a key is called.
//
// `onKeyPress` on a `<TextInput>` existed and a `<View>` had nothing, so an app
// could not bind a shortcut to anything it draws. On a phone that is no loss; on
// a desktop it is most of the interface.
//
// ## The vocabulary is a browser's
//
// A key is named the way `KeyboardEvent.key` names it: `"a"`, `"A"`, `" "`,
// `"ArrowLeft"`, `"Escape"`, `"F5"`. react-native-macos and react-native-windows
// both use these, so an app that binds `"ArrowLeft"` means the same thing on all
// five platforms it might run on. The names live here as constants rather than as
// string literals in three hosts, for the reason core/TestQuitFile.h gives about
// shared spellings: three places to write "ArrowLeft" is three places to write
// "ArrowLeft " instead, and the failure is a shortcut that silently never fires.
//
// Case follows the character, as it does in a browser: Shift+A is `"A"` and its
// `shift` modifier is also set. An app may bind either and both are honest.
//
// ## Why a view declares its keys in advance
//
// Because a host has to decide whether a key was handled *before* it can do
// anything else, and it cannot ask JavaScript at that moment. An unhandled key
// must still reach the application menu, a focused text field, or a scroll view
// that scrolls on arrows; a handled one must not. That decision is the return
// value of `performKeyEquivalent:`, of GTK's `key-pressed` handler, and of
// whether `WM_KEYDOWN` reaches `DefWindowProc` -- three synchronous answers, and
// a round trip through the JavaScript thread is not available to any of them.
//
// So `handledBy` is the whole point of this file: a host asks it, gets an answer
// in the same call stack, and consumes the key or passes it on accordingly.
// Reporting the press to JavaScript happens afterwards and asynchronously,
// because by then the decision has been made.
//
// ## Why the registry is here
//
// Three hosts asking "does this view claim Cmd+Z" is one decision, and a host
// keeping its own copy would be a fourth place for it to drift. The tags are
// React's, so nothing here knows what a view is.

#pragma once

#include <react/renderer/core/ReactPrimitives.h>

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace basalt {

// The names that are not a character. A host maps its own key codes onto these;
// what it must not do is spell them itself.
inline constexpr const char *kKeyArrowUp = "ArrowUp";
inline constexpr const char *kKeyArrowDown = "ArrowDown";
inline constexpr const char *kKeyArrowLeft = "ArrowLeft";
inline constexpr const char *kKeyArrowRight = "ArrowRight";
inline constexpr const char *kKeyEnter = "Enter";
inline constexpr const char *kKeyTab = "Tab";
inline constexpr const char *kKeyEscape = "Escape";
inline constexpr const char *kKeyBackspace = "Backspace";
inline constexpr const char *kKeyDelete = "Delete";
inline constexpr const char *kKeyHome = "Home";
inline constexpr const char *kKeyEnd = "End";
inline constexpr const char *kKeyPageUp = "PageUp";
inline constexpr const char *kKeyPageDown = "PageDown";
// A space is a character like any other, and is named `" "` rather than
// `"Space"`. Worth a constant because it is the one that looks like a mistake.
inline constexpr const char *kKeySpace = " ";

struct KeyModifiers {
  bool alt = false;
  bool ctrl = false;
  bool meta = false;
  bool shift = false;

  bool operator==(const KeyModifiers &other) const {
    return alt == other.alt && ctrl == other.ctrl && meta == other.meta &&
           shift == other.shift;
  }
  bool operator!=(const KeyModifiers &other) const { return !(*this == other); }
};

// A press, and also a claim: the two are compared for equality, so they are one
// shape rather than two that have to agree.
struct KeyCombination {
  std::string key;
  KeyModifiers modifiers;

  bool operator==(const KeyCombination &other) const {
    return key == other.key && modifiers == other.modifiers;
  }
};

// What a view claims. Replaces whatever it claimed before rather than adding to
// it: a re-render produces the whole list, and merging would leave a shortcut
// live after the app stopped declaring it.
void setHandledKeys(facebook::react::Tag tag, std::vector<KeyCombination> combinations);

// On unmount. Not on every mutation -- an empty list is a view that claims
// nothing and still exists.
void clearHandledKeys(facebook::react::Tag tag);

// Whether that one view claims it.
bool handlesKey(facebook::react::Tag tag, const KeyCombination &pressed);

// The nearest view in `path` that claims it, or nothing. `path` runs from the
// focused view outwards to the root, so the first match is the innermost -- the
// same walk `DragAndDrop`'s drop targets use, and the same reason: a shortcut on
// a pane should win over the same shortcut on the window.
std::optional<facebook::react::Tag> handledBy(
    const std::vector<facebook::react::Tag> &path, const KeyCombination &pressed);

// Any view that claims it, focus ignored. For the case where nothing has focus
// at all, which is the ordinary state of a window whose app has declared
// window-level shortcuts and focused nothing.
//
// **Only when nothing is focused.** A host with a focused view must use
// `handledBy`, because a claim outside the focus path is exactly how a background
// pane would steal a keystroke from a text field. With nothing focused there is no
// text field taking keys and no path to be outside of, so the choice is between a
// window-level shortcut working and it never firing -- and never firing is what a
// `<KeyHandler>` below the root got before this existed, which is every app that
// declares shortcuts once at the top.
//
// Ambiguous by construction when two views claim the same combination and neither
// is focused. The first found answers, and which that is depends on the registry's
// order -- so an app with two window-level handlers for one combination gets one
// of them and no promise which. Worth knowing rather than worth preventing: the
// alternative is refusing to fire at all.
std::optional<facebook::react::Tag> handledByAny(const KeyCombination &pressed);

// How many views currently claim anything. For tests, and for a host that wants
// to skip the walk entirely when nothing is listening.
std::size_t handledKeyViewCount();

// Reporting, after the host has already decided. Set by whatever carries events
// to JavaScript; absent until then, in which case a press is dropped rather than
// queued -- a shortcut that fires late is worse than one that did not fire.
void setKeyListener(std::function<void(facebook::react::Tag, const KeyCombination &)> listener);
void reportKey(facebook::react::Tag tag, const KeyCombination &pressed);

} // namespace basalt
