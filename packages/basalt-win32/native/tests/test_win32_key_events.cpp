// A Win32 key press as a key combination.
//
// The third of three mapping suites, and the one that most needs to exist: this
// host is the one nobody can compile on a Mac, so the only thing that checks the
// table below is CI. The two silent failures are the same as everywhere -- a
// misspelt name matches nothing and reports nothing, and a bare modifier offered
// as a combination fires a shortcut on Ctrl alone.
//
// What is *not* covered here is the modifiers. They come from `GetKeyState`
// rather than from the message, so asserting on them would mean holding a key
// down in a test process. The combination's key half is what this checks; the
// modifier half is checked on the two hosts where the state arrives with the
// event, and the three tables are written to agree.

#include "TestHarness.h"

#include "Win32KeyEvents.h"

#include <windows.h>

#include <sstream>
#include <string>

namespace {

std::string keyOf(unsigned int virtualKey) {
  const auto combination = basalt::keyCombinationFrom(virtualKey);
  return combination.has_value() ? combination->key : std::string("(none)");
}

} // namespace

TEST(win32_keys_the_arrows_have_browser_names) {
  EXPECT_EQ(keyOf(VK_LEFT), std::string("ArrowLeft"));
  EXPECT_EQ(keyOf(VK_RIGHT), std::string("ArrowRight"));
  EXPECT_EQ(keyOf(VK_UP), std::string("ArrowUp"));
  EXPECT_EQ(keyOf(VK_DOWN), std::string("ArrowDown"));
}

TEST(win32_keys_the_navigation_block_has_browser_names) {
  // VK_PRIOR and VK_NEXT are Page Up and Page Down. The names are about
  // priority, not paging, and getting them the wrong way round would be a
  // shortcut that scrolls the wrong direction -- plausible enough to survive.
  EXPECT_EQ(keyOf(VK_PRIOR), std::string("PageUp"));
  EXPECT_EQ(keyOf(VK_NEXT), std::string("PageDown"));
  EXPECT_EQ(keyOf(VK_HOME), std::string("Home"));
  EXPECT_EQ(keyOf(VK_END), std::string("End"));
  EXPECT_EQ(keyOf(VK_DELETE), std::string("Delete"));
  EXPECT_EQ(keyOf(VK_BACK), std::string("Backspace"));
}

TEST(win32_keys_escape_enter_tab_and_space) {
  EXPECT_EQ(keyOf(VK_ESCAPE), std::string("Escape"));
  EXPECT_EQ(keyOf(VK_RETURN), std::string("Enter"));
  EXPECT_EQ(keyOf(VK_TAB), std::string("Tab"));
  // A space is named " ". Unlike the other two hosts, Shift+Tab here is VK_TAB
  // with the modifier held rather than a key of its own, so there is nothing to
  // fold together.
  EXPECT_EQ(keyOf(VK_SPACE), std::string(" "));
}

TEST(win32_keys_a_bare_modifier_is_not_a_combination) {
  // On the other two hosts a bare modifier is detectable by producing no
  // character. Here the virtual key has to be recognised, so each one is listed
  // and each one is asserted.
  for (unsigned int key : {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL,
                           VK_RCONTROL, VK_MENU, VK_LMENU, VK_RMENU, VK_LWIN, VK_RWIN,
                           VK_CAPITAL}) {
    EXPECT_EQ(keyOf(key), std::string("(none)"));
  }
}

TEST(win32_keys_a_letter_is_lower_case_when_shift_is_not_held) {
  // MapVirtualKeyW hands back upper case for a letter whatever the shift state,
  // so the case is applied by this host rather than arriving with the event. No
  // key is held in a test process, so this is the unshifted answer.
  EXPECT_EQ(keyOf('A'), std::string("a"));
  EXPECT_EQ(keyOf('Z'), std::string("z"));
}

TEST(win32_keys_a_digit_is_itself) {
  EXPECT_EQ(keyOf('0'), std::string("0"));
  EXPECT_EQ(keyOf('7'), std::string("7"));
}
