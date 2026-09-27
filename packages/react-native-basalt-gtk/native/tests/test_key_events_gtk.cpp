// A GDK key press as a key combination.
//
// The same ten questions appkit/tests/test_appkit_key_events.mm asks, because the
// answers have to agree: an app that binds "ArrowLeft" or metaKey must get the
// same key on both desktops. Where the two hosts differ is what they start from
// -- a keyval here, an NSEvent there -- and that is the whole reason the names
// are constants in core rather than literals in each.

#include "TestHarness.h"

#include "GtkKeyEvents.h"

#include <sstream>
#include <string>

namespace {

std::string keyOf(guint keyval, GdkModifierType state = static_cast<GdkModifierType>(0)) {
  const auto combination = basalt::keyCombinationFrom(keyval, state);
  return combination.has_value() ? combination->key : std::string("(none)");
}

basalt::KeyModifiers modifiersOf(guint keyval, GdkModifierType state) {
  const auto combination = basalt::keyCombinationFrom(keyval, state);
  return combination.has_value() ? combination->modifiers : basalt::KeyModifiers{};
}

} // namespace

TEST(gtk_keys_a_plain_letter_is_itself) {
  EXPECT_EQ(keyOf(GDK_KEY_a), std::string("a"));
  EXPECT(modifiersOf(GDK_KEY_a, static_cast<GdkModifierType>(0)) == basalt::KeyModifiers{});
}

TEST(gtk_keys_case_comes_from_the_keyval) {
  // GDK gives Shift+A its own keyval, so the case is already right and nothing
  // here has to apply a modifier to get it. AppKit needs `characters` for the
  // same result, which is the difference between the two hosts in one line.
  EXPECT_EQ(keyOf(GDK_KEY_A, GDK_SHIFT_MASK), std::string("A"));
  EXPECT(modifiersOf(GDK_KEY_A, GDK_SHIFT_MASK).shift);
}

TEST(gtk_keys_super_is_meta_and_not_control) {
  // W3C names the Windows and Command keys Meta, and Super is the key in that
  // position here -- so an app binding metaKey presses the same physical key on
  // all three desktops.
  const auto modifiers = modifiersOf(GDK_KEY_z, GDK_SUPER_MASK);
  EXPECT(modifiers.meta);
  EXPECT(!modifiers.ctrl);
  EXPECT_EQ(keyOf(GDK_KEY_z, GDK_SUPER_MASK), std::string("z"));
}

TEST(gtk_keys_control_is_control) {
  const auto modifiers = modifiersOf(GDK_KEY_a, GDK_CONTROL_MASK);
  EXPECT(modifiers.ctrl);
  EXPECT(!modifiers.meta);
  // And the letter, not a control character: a keyval is the key rather than
  // what it would insert, so this comes out right without the correction the
  // AppKit host needs.
  EXPECT_EQ(keyOf(GDK_KEY_a, GDK_CONTROL_MASK), std::string("a"));
}

TEST(gtk_keys_the_arrows_have_browser_names) {
  EXPECT_EQ(keyOf(GDK_KEY_Left), std::string("ArrowLeft"));
  EXPECT_EQ(keyOf(GDK_KEY_Right), std::string("ArrowRight"));
  EXPECT_EQ(keyOf(GDK_KEY_Up), std::string("ArrowUp"));
  EXPECT_EQ(keyOf(GDK_KEY_Down), std::string("ArrowDown"));
  // The keypad's arrows are the same keys to an app. A person with Num Lock off
  // pressing the 4 key means left.
  EXPECT_EQ(keyOf(GDK_KEY_KP_Left), std::string("ArrowLeft"));
}

TEST(gtk_keys_the_navigation_block_has_browser_names) {
  EXPECT_EQ(keyOf(GDK_KEY_Page_Up), std::string("PageUp"));
  EXPECT_EQ(keyOf(GDK_KEY_Home), std::string("Home"));
  EXPECT_EQ(keyOf(GDK_KEY_End), std::string("End"));
  // Forward delete against backspace, which is the pair most easily conflated.
  EXPECT_EQ(keyOf(GDK_KEY_Delete), std::string("Delete"));
  EXPECT_EQ(keyOf(GDK_KEY_BackSpace), std::string("Backspace"));
}

TEST(gtk_keys_escape_enter_tab_and_space) {
  EXPECT_EQ(keyOf(GDK_KEY_Escape), std::string("Escape"));
  EXPECT_EQ(keyOf(GDK_KEY_Return), std::string("Enter"));
  EXPECT_EQ(keyOf(GDK_KEY_KP_Enter), std::string("Enter"));
  EXPECT_EQ(keyOf(GDK_KEY_Tab), std::string("Tab"));
  // A space is a character, named " ". gdk_keyval_to_unicode gives it directly,
  // so this is the same answer by a different route than AppKit's.
  EXPECT_EQ(keyOf(GDK_KEY_space), std::string(" "));
}

TEST(gtk_keys_shift_tab_is_tab_with_shift) {
  // Its own keyval here, as back-tab is its own character on AppKit.
  EXPECT_EQ(keyOf(GDK_KEY_ISO_Left_Tab, GDK_SHIFT_MASK), std::string("Tab"));
  EXPECT(modifiersOf(GDK_KEY_ISO_Left_Tab, GDK_SHIFT_MASK).shift);
}

TEST(gtk_keys_a_bare_modifier_is_not_a_combination) {
  // Pressing Control on its own arrives as a keyval with no character. Offering
  // it as a combination would fire a shortcut on the modifier alone.
  EXPECT_EQ(keyOf(GDK_KEY_Control_L, GDK_CONTROL_MASK), std::string("(none)"));
  EXPECT_EQ(keyOf(GDK_KEY_Shift_L, GDK_SHIFT_MASK), std::string("(none)"));
}

TEST(gtk_keys_several_modifiers_at_once) {
  const auto modifiers =
      modifiersOf(GDK_KEY_z, static_cast<GdkModifierType>(GDK_SUPER_MASK | GDK_SHIFT_MASK));
  EXPECT(modifiers.meta);
  EXPECT(modifiers.shift);
  EXPECT(!modifiers.alt);
  EXPECT(!modifiers.ctrl);
}
