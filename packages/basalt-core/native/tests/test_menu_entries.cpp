// The portable half of a popup menu: how its entries are numbered, which of them
// are drawn, and where a radio group begins and ends.
//
// This is where a popup menu's real bugs have been, and none of them were in the
// drawing. An index is an entry's position in a pre-order walk of the whole menu,
// and both sides of a JSI boundary have to agree on it: the hosts tag their items
// with it and `itemAt` in useContextMenu.ts walks the tree the app passed. The
// first attempt at roles filtered unsupported ones out of the vector, which would
// have had the two sides numbering different lists, and on Linux every item after
// an `about` would have run the handler of the one before it.
//
// The drawing is not here and is not asserted anywhere: `presentMenu` answers
// BASALT_TEST_MENU before `showMenu` is reached, so an automated run never builds
// a native menu at all. See docs/backlog/desktop-capabilities.md.

#include "TestHarness.h"

#include "PlatformServices.h"

#include <sstream>
#include <string>
#include <vector>

namespace {

using basalt::MenuEntry;

MenuEntry item(std::string label) {
  MenuEntry entry;
  entry.label = std::move(label);
  return entry;
}

MenuEntry radio(std::string label, bool checked) {
  MenuEntry entry = item(std::move(label));
  entry.mark = MenuEntry::Mark::Radio;
  entry.checked = checked;
  return entry;
}

MenuEntry check(std::string label, bool checked) {
  MenuEntry entry = item(std::move(label));
  entry.mark = MenuEntry::Mark::Check;
  entry.checked = checked;
  return entry;
}

MenuEntry parent(std::string label, std::vector<MenuEntry> children) {
  MenuEntry entry = item(std::move(label));
  entry.submenu = std::move(children);
  return entry;
}

} // namespace

TEST(menu_indexes_are_preorder_and_count_separators_and_parents) {
  // A flat list is numbered by position, which is what an index meant before a
  // popup could nest, so no caller's meaning changed when nesting arrived.
  const std::vector<MenuEntry> flat = {item("a"), MenuEntry::separator(), item("b")};
  EXPECT_EQ(basalt::menuEntryCount(flat), 3);
  EXPECT_EQ(basalt::menuEntryAt(flat, 0)->label, std::string("a"));
  EXPECT(basalt::menuEntryAt(flat, 1)->isSeparator());
  EXPECT_EQ(basalt::menuEntryAt(flat, 2)->label, std::string("b"));

  // Nested: the parent, then its children, then what follows it.
  const std::vector<MenuEntry> nested = {
      item("first"),
      parent("branch", {item("leaf one"), item("leaf two")}),
      item("last"),
  };
  EXPECT_EQ(basalt::menuEntryCount(nested), 5);
  EXPECT_EQ(basalt::menuEntryAt(nested, 0)->label, std::string("first"));
  EXPECT_EQ(basalt::menuEntryAt(nested, 1)->label, std::string("branch"));
  EXPECT_EQ(basalt::menuEntryAt(nested, 2)->label, std::string("leaf one"));
  EXPECT_EQ(basalt::menuEntryAt(nested, 3)->label, std::string("leaf two"));
  // The one that matters: a parent's children do not push what follows it off by
  // their own count, they are counted where they are.
  EXPECT_EQ(basalt::menuEntryAt(nested, 4)->label, std::string("last"));
  EXPECT(basalt::menuEntryAt(nested, 5) == nullptr);
}

TEST(menu_entries_the_platform_drops_still_count) {
  // A role this desktop cannot perform is not drawn. It keeps its index anyway,
  // because the index is into the list JavaScript passed: numbering only what was
  // drawn would move every entry after it onto the wrong handler.
  MenuEntry unsupported = item("about");
  unsupported.role = "not-a-role-any-platform-has";
  const std::vector<MenuEntry> entries = {item("before"), unsupported, item("after")};

  EXPECT(!basalt::menuEntryShown(entries[1]));
  EXPECT(basalt::menuEntryShown(entries[0]));
  EXPECT_EQ(basalt::menuEntryCount(entries), 3);
  EXPECT_EQ(basalt::menuEntryAt(entries, 2)->label, std::string("after"));
}

TEST(a_parent_with_nothing_drawable_under_it_is_not_drawn) {
  MenuEntry unsupported = item("about");
  unsupported.role = "not-a-role-any-platform-has";
  const MenuEntry empty = parent("branch", {unsupported});
  EXPECT(!basalt::menuEntryShown(empty));

  // One drawable child is enough to keep the parent.
  MenuEntry another = item("about");
  another.role = "not-a-role-any-platform-has";
  const MenuEntry mixed = parent("branch", {another, item("real")});
  EXPECT(basalt::menuEntryShown(mixed));
}

TEST(a_radio_run_is_the_adjacent_radio_siblings) {
  // Ended by a non-radio entry, by a separator, and by the end of the level.
  const std::vector<MenuEntry> entries = {
      item("plain"),            // 0
      radio("one", true),       // 1
      radio("two", false),      // 2
      MenuEntry::separator(),   // 3
      radio("three", false),    // 4
      radio("four", false),     // 5
  };

  // The first run is 1..3, stopping at the separator rather than running on.
  EXPECT_EQ(basalt::menuRadioRun(entries, 0, 1).first, 1);
  EXPECT_EQ(basalt::menuRadioRun(entries, 0, 1).second, 3);
  // Asking about any member gives the same run, which is what lets every member
  // of a group share one action named after where the run starts.
  EXPECT_EQ(basalt::menuRadioRun(entries, 0, 2).first, 1);
  EXPECT_EQ(basalt::menuRadioRun(entries, 0, 2).second, 3);
  // The second run is 4..6, ended by the end of the level.
  EXPECT_EQ(basalt::menuRadioRun(entries, 0, 4).first, 4);
  EXPECT_EQ(basalt::menuRadioRun(entries, 0, 4).second, 6);
  // A non-radio entry is its own degenerate run, so a caller need not special-case.
  EXPECT_EQ(basalt::menuRadioRun(entries, 0, 0).first, 0);
  EXPECT_EQ(basalt::menuRadioRun(entries, 0, 0).second, 0);
}

TEST(a_radio_run_is_offset_by_the_level_it_is_on) {
  // Inside a submenu the indexes are the pre-order ones, not positions in the
  // sibling vector, which is what `firstIndexOfLevel` carries. A run named after
  // a sibling position would collide between levels.
  const std::vector<MenuEntry> children = {radio("a", false), radio("b", true)};
  EXPECT_EQ(basalt::menuRadioRun(children, 7, 0).first, 7);
  EXPECT_EQ(basalt::menuRadioRun(children, 7, 0).second, 9);
  EXPECT_EQ(basalt::menuRadioRun(children, 7, 1).first, 7);
}

TEST(a_mark_does_not_change_whether_an_entry_is_drawn_or_counted) {
  // A checked item is an ordinary choosable leaf. All three desktops can draw a
  // mark, so nothing here is dropped for carrying one, and the index contract is
  // untouched by it.
  const std::vector<MenuEntry> entries = {check("ticked", true), radio("chosen", true),
                                          check("unticked", false)};
  for (const MenuEntry &entry : entries) {
    EXPECT(basalt::menuEntryShown(entry));
    EXPECT(!entry.isSeparator());
    EXPECT(!entry.isParent());
  }
  EXPECT_EQ(basalt::menuEntryCount(entries), 3);
}
