// Which view claims which key. The decision, not the delivery.
//
// A host asks `handledBy` in the middle of `keyDown:`, `key-pressed` or
// `WM_KEYDOWN` and consumes the key or passes it on by the answer. So the two
// failures that matter are opposite and both silent: claiming a key nobody
// declared swallows it from the menu and from text fields, and failing to claim
// one that was declared means a shortcut that quietly never fires.

#include "TestHarness.h"

#include "KeyEvents.h"
#include "TestKeys.h"

#include <sstream>
#include <string>
#include <vector>

using basalt::KeyCombination;
using basalt::KeyModifiers;

namespace {

KeyCombination press(std::string key, KeyModifiers modifiers = {}) {
  return KeyCombination{std::move(key), modifiers};
}

KeyModifiers meta() { return KeyModifiers{.alt = false, .ctrl = false, .meta = true, .shift = false}; }
KeyModifiers metaShift() {
  return KeyModifiers{.alt = false, .ctrl = false, .meta = true, .shift = true};
}

// Tags are React's and mean nothing here, so they are just distinct numbers.
constexpr facebook::react::Tag kPane = 11;
constexpr facebook::react::Tag kWindow = 3;
constexpr facebook::react::Tag kUnrelated = 99;

void reset() {
  for (facebook::react::Tag tag : {kPane, kWindow, kUnrelated}) {
    basalt::clearHandledKeys(tag);
  }
}

} // namespace

TEST(keys_a_view_that_claimed_nothing_claims_nothing) {
  reset();
  // The common case by a wide margin: no app in this repository's suite declares
  // a key, and every one of them presses some.
  EXPECT(!basalt::handlesKey(kPane, press("a")));
  EXPECT(!basalt::handledBy({kPane, kWindow}, press("a")).has_value());
  EXPECT_EQ((int)basalt::handledKeyViewCount(), 0);
}

TEST(keys_a_claim_matches_on_key_and_modifiers) {
  reset();
  basalt::setHandledKeys(kPane, {press("z", meta())});

  EXPECT(basalt::handlesKey(kPane, press("z", meta())));
  // Same key, no modifier: a different combination, and an app that bound Cmd+Z
  // did not bind Z.
  EXPECT(!basalt::handlesKey(kPane, press("z")));
  // Same modifiers, different key.
  EXPECT(!basalt::handlesKey(kPane, press("y", meta())));
  // A modifier the claim did not ask for. Shift+Cmd+Z is redo and is its own
  // binding; matching it here would fire undo.
  EXPECT(!basalt::handlesKey(kPane, press("z", metaShift())));
}

TEST(keys_case_follows_the_character) {
  reset();
  // As in a browser: Shift+A reports "A" with shift set, and an app may bind
  // either. They are different combinations and neither is wrong.
  basalt::setHandledKeys(kPane, {press("A", KeyModifiers{.shift = true})});
  EXPECT(basalt::handlesKey(kPane, press("A", KeyModifiers{.shift = true})));
  EXPECT(!basalt::handlesKey(kPane, press("a", KeyModifiers{.shift = true})));
}

TEST(keys_the_innermost_claim_wins) {
  reset();
  // Both claim it. The path runs from the focused view outwards, so the pane
  // answers -- a shortcut on a pane should beat the same one on the window.
  basalt::setHandledKeys(kPane, {press(basalt::kKeyEscape)});
  basalt::setHandledKeys(kWindow, {press(basalt::kKeyEscape)});

  const auto handler = basalt::handledBy({kPane, kWindow}, press(basalt::kKeyEscape));
  EXPECT(handler.has_value());
  EXPECT_EQ((int)*handler, (int)kPane);

  // And with the pane out of the path, the window answers: an ancestor's claim
  // is not shadowed by a descendant that exists, only by one in the path.
  const auto outer = basalt::handledBy({kWindow}, press(basalt::kKeyEscape));
  EXPECT(outer.has_value());
  EXPECT_EQ((int)*outer, (int)kWindow);
}

TEST(keys_a_view_outside_the_path_is_not_asked) {
  reset();
  // The focus path is the whole of it. A view elsewhere in the tree claiming a
  // key must not swallow it -- that is how a background pane would steal typing.
  basalt::setHandledKeys(kUnrelated, {press("j")});
  EXPECT(!basalt::handledBy({kPane, kWindow}, press("j")).has_value());
}

TEST(keys_a_new_declaration_replaces_the_old) {
  reset();
  basalt::setHandledKeys(kPane, {press("j"), press("k"), press("l")});
  EXPECT(basalt::handlesKey(kPane, press("k")));

  // A re-render hands over the whole list. Merging would keep "k" alive after
  // the app stopped declaring it, which would look like the shortcut working --
  // the failure this list being assigned rather than appended exists to prevent.
  basalt::setHandledKeys(kPane, {press("j"), press("l")});
  EXPECT(basalt::handlesKey(kPane, press("j")));
  EXPECT(!basalt::handlesKey(kPane, press("k")));
  EXPECT_EQ((int)basalt::handledKeyViewCount(), 1);
}

TEST(keys_an_empty_declaration_is_not_the_same_as_unmounting) {
  reset();
  basalt::setHandledKeys(kPane, {press("j")});
  basalt::setHandledKeys(kPane, {});
  // Still registered, claiming nothing: a view whose shortcuts are all disabled
  // exists and may get them back.
  EXPECT_EQ((int)basalt::handledKeyViewCount(), 1);
  EXPECT(!basalt::handlesKey(kPane, press("j")));

  basalt::clearHandledKeys(kPane);
  EXPECT_EQ((int)basalt::handledKeyViewCount(), 0);
}

TEST(keys_a_press_reaches_the_listener) {
  reset();
  basalt::setHandledKeys(kPane, {press("m")});

  int calls = 0;
  facebook::react::Tag seenTag = 0;
  std::string seenKey;
  bool seenShift = false;
  basalt::setKeyListener([&](facebook::react::Tag tag, const KeyCombination &pressed) {
    calls++;
    seenTag = tag;
    seenKey = pressed.key;
    seenShift = pressed.modifiers.shift;
  });

  basalt::reportKey(kPane, press("m", KeyModifiers{.shift = true}));
  EXPECT_EQ(calls, 1);
  EXPECT_EQ((int)seenTag, (int)kPane);
  EXPECT_EQ(seenKey, std::string("m"));
  EXPECT(seenShift);

  // Reported as given, not re-matched: the host has already decided, and a
  // second opinion here could disagree with the one the key was consumed on.
  basalt::reportKey(kPane, press("not-a-key"));
  EXPECT_EQ(calls, 2);

  basalt::setKeyListener(nullptr);
  basalt::reportKey(kPane, press("m"));
  EXPECT_EQ(calls, 2);
}

TEST(keys_the_special_names_are_the_ones_a_browser_uses) {
  // Spelled once, here, because three hosts mapping their own key codes is three
  // chances to write "Arrowleft" -- and the failure is a shortcut that silently
  // never fires, on one platform only. core/TestQuitFile.h has the same note for
  // the same reason.
  EXPECT_EQ(std::string(basalt::kKeyArrowLeft), std::string("ArrowLeft"));
  EXPECT_EQ(std::string(basalt::kKeyArrowRight), std::string("ArrowRight"));
  EXPECT_EQ(std::string(basalt::kKeyArrowUp), std::string("ArrowUp"));
  EXPECT_EQ(std::string(basalt::kKeyArrowDown), std::string("ArrowDown"));
  EXPECT_EQ(std::string(basalt::kKeyEscape), std::string("Escape"));
  EXPECT_EQ(std::string(basalt::kKeyEnter), std::string("Enter"));
  EXPECT_EQ(std::string(basalt::kKeyTab), std::string("Tab"));
  EXPECT_EQ(std::string(basalt::kKeyPageUp), std::string("PageUp"));
  EXPECT_EQ(std::string(basalt::kKeyPageDown), std::string("PageDown"));
  // The one that looks like a mistake and is not: a space is a character.
  EXPECT_EQ(std::string(basalt::kKeySpace), std::string(" "));
}


// --- BASALT_TEST_KEY's syntax ----------------------------------------------
//
// Parsed in core rather than in three hosts, because three parsers is three
// chances to disagree about what "z+meta" means -- and to disagree quietly: the
// wrong answer is a combination that matches nothing, which a scenario reports as
// a shortcut that did not fire rather than as a bad instrument.

TEST(keys_the_instrument_reads_a_bare_key) {
  const KeyCombination pressed = basalt::parseKeyPress("m");
  EXPECT_EQ(pressed.key, std::string("m"));
  EXPECT(pressed.modifiers == KeyModifiers{});
}

TEST(keys_the_instrument_reads_modifiers) {
  const KeyCombination undo = basalt::parseKeyPress("z+meta");
  EXPECT_EQ(undo.key, std::string("z"));
  EXPECT(undo.modifiers.meta);
  EXPECT(!undo.modifiers.shift);

  const KeyCombination redo = basalt::parseKeyPress("z+meta+shift");
  EXPECT(redo.modifiers.meta);
  EXPECT(redo.modifiers.shift);
  // The pair that must differ, or a test for redo would pass on undo.
  EXPECT(!(undo == redo));
}

TEST(keys_the_instrument_reads_a_named_key) {
  EXPECT_EQ(basalt::parseKeyPress("ArrowLeft").key, std::string("ArrowLeft"));
  // A space is a key named " ", and writing it in an environment variable means
  // the parser must not trim.
  EXPECT_EQ(basalt::parseKeyPress(" ").key, std::string(" "));
}

TEST(keys_the_instrument_reads_a_literal_plus) {
  // "+" is both the separator and a key an editor binds -- zoom in. Spelt by
  // being first, which is the one case where an empty piece means something.
  EXPECT_EQ(basalt::parseKeyPress("+").key, std::string("+"));
  const KeyCombination zoom = basalt::parseKeyPress("++meta");
  EXPECT_EQ(zoom.key, std::string("+"));
  EXPECT(zoom.modifiers.meta);
}

TEST(keys_the_instrument_ignores_a_modifier_it_does_not_know) {
  // A typo gives a combination that matches nothing rather than an error, and the
  // scenario then reports a shortcut that did not fire -- the honest symptom.
  const KeyCombination pressed = basalt::parseKeyPress("z+command");
  EXPECT_EQ(pressed.key, std::string("z"));
  EXPECT(pressed.modifiers == KeyModifiers{});
}

TEST(keys_the_instrument_is_empty_when_unset) {
  // Every scenario that does not press a key relies on this, which is all but
  // one of them.
  EXPECT(basalt::scriptedKeyPresses().empty());
}
