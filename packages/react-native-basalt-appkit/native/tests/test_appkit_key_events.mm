// An NSEvent as a key combination.
//
// The mapping is the part of key handling that is this host's alone -- the
// registry and the matching are core's, and tested there -- and it is where the
// silent failures live. A key named "Arrowleft" matches nothing and reports
// nothing, and an app that bound "ArrowLeft" simply finds its shortcut dead.

#include "TestHarness.h"

#import "AppKitKeyEvents.h"

#import <AppKit/AppKit.h>

#include <sstream>
#include <string>

namespace {

// AppKit does not let you make an NSEvent from nothing, but it does let you
// describe one. The characters are what a real press would carry: `characters`
// with modifiers applied, `charactersIgnoringModifiers` without.
NSEvent *keyPress(NSString *characters, NSString *bare, NSEventModifierFlags flags) {
  return [NSEvent keyEventWithType:NSEventTypeKeyDown
                         location:NSZeroPoint
                    modifierFlags:flags
                        timestamp:0
                     windowNumber:0
                          context:nil
                       characters:characters
      charactersIgnoringModifiers:bare
                        isARepeat:NO
                          keyCode:0];
}

std::string keyOf(NSEvent *event) {
  const auto combination = basalt::keyCombinationFrom(event);
  return combination.has_value() ? combination->key : std::string("(none)");
}

basalt::KeyModifiers modifiersOf(NSEvent *event) {
  const auto combination = basalt::keyCombinationFrom(event);
  return combination.has_value() ? combination->modifiers : basalt::KeyModifiers{};
}

// A character in the private-use area, which is how AppKit reports the arrows.
NSString *functionKey(unichar code) {
  return [NSString stringWithCharacters:&code length:1];
}

} // namespace

TEST(appkit_keys_a_plain_letter_is_itself) {
  EXPECT_EQ(keyOf(keyPress(@"a", @"a", 0)), std::string("a"));
  EXPECT(modifiersOf(keyPress(@"a", @"a", 0)) == basalt::KeyModifiers{});
}

TEST(appkit_keys_shift_changes_the_character_and_sets_the_modifier) {
  // Both, as a browser reports both: the key is "A" and shiftKey is true. An app
  // may bind either and neither is wrong -- which is only true if the character
  // comes from `characters` rather than `charactersIgnoringModifiers`.
  NSEvent *event = keyPress(@"A", @"a", NSEventModifierFlagShift);
  EXPECT_EQ(keyOf(event), std::string("A"));
  EXPECT(modifiersOf(event).shift);
}

TEST(appkit_keys_command_is_meta) {
  // As in a browser on this platform, and as react-native-macos spells it: an
  // app binding Cmd+Z writes metaKey. Calling it `cmd` would have been clearer
  // here and wrong everywhere an app is shared.
  NSEvent *event = keyPress(@"z", @"z", NSEventModifierFlagCommand);
  EXPECT_EQ(keyOf(event), std::string("z"));
  EXPECT(modifiersOf(event).meta);
  EXPECT(!modifiersOf(event).ctrl);
}

TEST(appkit_keys_control_reports_the_letter_not_the_control_character) {
  // Ctrl+A arrives with `characters` as 0x01, which is not a name anybody binds.
  // The unmodified character is what the app means.
  NSEvent *event = keyPress(@"\x01", @"a", NSEventModifierFlagControl);
  EXPECT_EQ(keyOf(event), std::string("a"));
  EXPECT(modifiersOf(event).ctrl);
}

TEST(appkit_keys_the_arrows_have_browser_names) {
  EXPECT_EQ(keyOf(keyPress(functionKey(NSLeftArrowFunctionKey),
                           functionKey(NSLeftArrowFunctionKey), 0)),
            std::string("ArrowLeft"));
  EXPECT_EQ(keyOf(keyPress(functionKey(NSRightArrowFunctionKey),
                           functionKey(NSRightArrowFunctionKey), 0)),
            std::string("ArrowRight"));
  EXPECT_EQ(keyOf(keyPress(functionKey(NSUpArrowFunctionKey),
                           functionKey(NSUpArrowFunctionKey), 0)),
            std::string("ArrowUp"));
  EXPECT_EQ(keyOf(keyPress(functionKey(NSDownArrowFunctionKey),
                           functionKey(NSDownArrowFunctionKey), 0)),
            std::string("ArrowDown"));
}

TEST(appkit_keys_the_navigation_block_has_browser_names) {
  EXPECT_EQ(keyOf(keyPress(functionKey(NSPageUpFunctionKey),
                           functionKey(NSPageUpFunctionKey), 0)),
            std::string("PageUp"));
  EXPECT_EQ(keyOf(keyPress(functionKey(NSHomeFunctionKey),
                           functionKey(NSHomeFunctionKey), 0)),
            std::string("Home"));
  // Forward delete is "Delete"; the key most keyboards label delete is
  // backspace, and conflating them is the mistake this asserts against.
  EXPECT_EQ(keyOf(keyPress(functionKey(NSDeleteFunctionKey),
                           functionKey(NSDeleteFunctionKey), 0)),
            std::string("Delete"));
  EXPECT_EQ(keyOf(keyPress(@"\x7F", @"\x7F", 0)), std::string("Backspace"));
}

TEST(appkit_keys_escape_enter_tab_and_space) {
  EXPECT_EQ(keyOf(keyPress(@"\x1B", @"\x1B", 0)), std::string("Escape"));
  EXPECT_EQ(keyOf(keyPress(@"\r", @"\r", 0)), std::string("Enter"));
  EXPECT_EQ(keyOf(keyPress(@"\t", @"\t", 0)), std::string("Tab"));
  // A space is a character and is named " ", not "Space". The one that looks
  // like a mistake.
  EXPECT_EQ(keyOf(keyPress(@" ", @" ", 0)), std::string(" "));
}

TEST(appkit_keys_shift_tab_is_tab_with_shift) {
  // AppKit sends back-tab as its own character rather than Tab with a modifier.
  // Reported as Tab plus shift, because that is what a browser reports and what
  // an app will have bound.
  NSEvent *event = keyPress(@"\x19", @"\x19", NSEventModifierFlagShift);
  EXPECT_EQ(keyOf(event), std::string("Tab"));
  EXPECT(modifiersOf(event).shift);
}

TEST(appkit_keys_a_bare_modifier_is_not_a_combination) {
  // Holding Command produces an event with no characters. Offering that to an
  // app as a combination would fire a shortcut on the modifier alone.
  EXPECT_EQ(keyOf(keyPress(@"", @"", NSEventModifierFlagCommand)), std::string("(none)"));
  EXPECT_EQ(keyOf(nil), std::string("(none)"));
}

TEST(appkit_keys_several_modifiers_at_once) {
  NSEvent *event = keyPress(@"z", @"z",
                            NSEventModifierFlagCommand | NSEventModifierFlagShift);
  const basalt::KeyModifiers modifiers = modifiersOf(event);
  EXPECT(modifiers.meta);
  EXPECT(modifiers.shift);
  EXPECT(!modifiers.alt);
  EXPECT(!modifiers.ctrl);
  // And it is a different combination from Cmd+Z, which is why redo does not
  // fire undo. core/KeyEvents.h asserts the matching half of this.
  EXPECT_EQ(keyOf(event), std::string("z"));
}
