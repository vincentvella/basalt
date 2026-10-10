// The menu an action sheet becomes, and reading an answer back out of it.
//
// All of the interesting behaviour is the index arithmetic, which is why it is a
// function rather than part of the module: `ActionSheetIOS` numbers the array it
// passed, a menu index counts every entry including the title and the message,
// and getting that wrong means an app runs the wrong choice -- the quietest
// possible bug, because every index is a valid index.
//
// What is asserted here and nowhere else: a dismissal is not an option, a
// header cannot be chosen, and the offset is exactly the number of entries the
// title and message took.

#include "TestHarness.h"

#include "ActionSheet.h"

#include <sstream>
#include <string>

using basalt::ActionSheetMenu;
using basalt::ActionSheetRequest;
using basalt::actionSheetChoice;
using basalt::actionSheetMenu;

namespace {

ActionSheetRequest threeOptions() {
  ActionSheetRequest request;
  request.options = {"Save", "Discard", "Cancel"};
  request.cancelIndex = 2;
  return request;
}

} // namespace

TEST(action_sheet_options_with_no_title_start_at_the_top) {
  const ActionSheetRequest request = threeOptions();
  const ActionSheetMenu menu = actionSheetMenu(request);
  EXPECT_EQ(menu.offset, 0);
  EXPECT_EQ((long)menu.menu.entries.size(), 3L);
  EXPECT_EQ(menu.menu.entries[0].label, std::string("Save"));
  // Negative is "wherever the pointer is", which is what a sheet with no anchor
  // wants and what MenuRequest already means by it.
  EXPECT(menu.menu.x < 0.0);
  EXPECT(menu.menu.y < 0.0);
}

TEST(action_sheet_a_title_and_a_message_are_disabled_entries_above_a_separator) {
  ActionSheetRequest request = threeOptions();
  request.title = "Unsaved changes";
  request.message = "Three files have not been written";

  const ActionSheetMenu menu = actionSheetMenu(request);
  EXPECT_EQ(menu.offset, 3);
  EXPECT_EQ((long)menu.menu.entries.size(), 6L);
  EXPECT_EQ(menu.menu.entries[0].label, std::string("Unsaved changes"));
  EXPECT(!menu.menu.entries[0].enabled);
  EXPECT(!menu.menu.entries[1].enabled);
  EXPECT(menu.menu.entries[2].isSeparator());
  EXPECT_EQ(menu.menu.entries[3].label, std::string("Save"));
  EXPECT(menu.menu.entries[3].enabled);
}

TEST(action_sheet_a_message_alone_still_takes_a_separator) {
  ActionSheetRequest request = threeOptions();
  request.message = "Three files have not been written";
  const ActionSheetMenu menu = actionSheetMenu(request);
  EXPECT_EQ(menu.offset, 2);
  EXPECT(menu.menu.entries[1].isSeparator());
}

TEST(action_sheet_disabled_indices_grey_the_option_they_name) {
  ActionSheetRequest request = threeOptions();
  request.disabledIndices = {1};
  request.title = "Unsaved changes";

  const ActionSheetMenu menu = actionSheetMenu(request);
  // Offset 2: a title and its separator. The disabled option is the app's
  // index 1, which is this menu's index 3.
  EXPECT_EQ(menu.offset, 2);
  EXPECT(menu.menu.entries[2].enabled);
  EXPECT(!menu.menu.entries[3].enabled);
  EXPECT(menu.menu.entries[4].enabled);
}

TEST(action_sheet_an_option_chosen_is_the_apps_own_index) {
  ActionSheetRequest request = threeOptions();
  request.title = "Unsaved changes";
  const ActionSheetMenu menu = actionSheetMenu(request);

  // A title and its separator, so menu index 2 is the first option -- which the
  // app knows as 0. This is the whole reason the offset is carried rather than
  // recomputed, and the arithmetic is the one thing here that can be silently
  // wrong: every index is a valid index.
  EXPECT_EQ(menu.offset, 2);
  EXPECT(actionSheetChoice(request, menu, 2).has_value());
  EXPECT_EQ(*actionSheetChoice(request, menu, 2), 0);
  EXPECT_EQ(*actionSheetChoice(request, menu, 4), 2);
  // One past the last option, which the menu does not have.
  EXPECT(!actionSheetChoice(request, menu, 5).has_value());
}

TEST(action_sheet_a_header_is_not_a_choice) {
  ActionSheetRequest request = threeOptions();
  request.title = "Unsaved changes";
  const ActionSheetMenu menu = actionSheetMenu(request);
  // A person cannot click a disabled entry and a script cannot either, but the
  // arithmetic has to refuse it on its own: index 0 minus an offset of 2 is not
  // an option, and reporting -2 to an app would be worse than reporting nothing.
  EXPECT(!actionSheetChoice(request, menu, 0).has_value());
  EXPECT(!actionSheetChoice(request, menu, 1).has_value());
}

TEST(action_sheet_an_index_past_the_end_is_not_a_choice) {
  const ActionSheetRequest request = threeOptions();
  const ActionSheetMenu menu = actionSheetMenu(request);
  EXPECT(!actionSheetChoice(request, menu, 3).has_value());
  EXPECT(!actionSheetChoice(request, menu, 99).has_value());
}

TEST(action_sheet_a_dismissal_is_the_cancel_choice_when_there_is_one) {
  // The one place this platform answers something iOS does not. Escape closing
  // a menu is ordinary where an iPad popover being tapped away is not, and an
  // app that named a cancel choice has already said what to do with it.
  const ActionSheetRequest request = threeOptions();
  const ActionSheetMenu menu = actionSheetMenu(request);
  EXPECT(actionSheetChoice(request, menu, -1).has_value());
  EXPECT_EQ(*actionSheetChoice(request, menu, -1), 2);
}

TEST(action_sheet_a_dismissal_with_no_cancel_choice_reports_nothing) {
  // Nothing rather than -1: `UIAlertController` calls the callback from a
  // button's handler, so a sheet that goes away without one calls nothing, and
  // an app never has to understand an index that is not an index.
  ActionSheetRequest request = threeOptions();
  request.cancelIndex = -1;
  const ActionSheetMenu menu = actionSheetMenu(request);
  EXPECT(!actionSheetChoice(request, menu, -1).has_value());
}

TEST(action_sheet_a_cancel_index_the_options_do_not_have_reports_nothing) {
  // An app that passed a stale index. Reporting it would be reporting an index
  // into an array that is too short, which is a crash in somebody's handler.
  ActionSheetRequest request = threeOptions();
  request.cancelIndex = 7;
  const ActionSheetMenu menu = actionSheetMenu(request);
  EXPECT(!actionSheetChoice(request, menu, -1).has_value());
}

TEST(action_sheet_an_anchor_puts_the_menu_where_the_caller_said) {
  ActionSheetRequest request = threeOptions();
  request.x = 120.0;
  request.y = 240.0;
  const ActionSheetMenu menu = actionSheetMenu(request);
  EXPECT_NEAR(menu.menu.x, 120.0, 0.01);
  EXPECT_NEAR(menu.menu.y, 240.0, 0.01);
}
