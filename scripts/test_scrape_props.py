#!/usr/bin/env python3
"""Tests for scripts/scrape_props.py, the part that cannot be checked by running it.

The scrape itself is checked by running it: `--check` fetches React Native at
the pin and fails if the committed inventory is not what it reads. What that
cannot check is the *accounting* -- whether a style name an app can write is
answered for anywhere -- because the real data is correct today, and the failure
this exists for is a name nobody noticed was missing.

Which is not hypothetical. `padding` and `margin` were absent from the support
page for as long as the page existed, because the three structs it scraped do
not declare them: Yoga keeps them in `yoga::Style` behind one field. Nothing
could say so, and a reader looking for `padding` found a page that looked
complete. These tests are about the thing that now says so.

Run with:  python3 scripts/test_scrape_props.py
"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


def load():
    """The scraper, imported from its path: it is a script rather than a module."""
    spec = importlib.util.spec_from_file_location(
        "scrape_props", REPO / "scripts" / "scrape_props.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


scrape_props = load()


def struct(props):
    return [{"name": "AStruct", "props": props}]


class Accounting(unittest.TestCase):
    def test_a_field_accounts_for_its_own_name(self):
        layout, unread, problems = scrape_props.account(
            {"opacity"}, struct([{"name": "opacity"}]), [], []
        )
        self.assertEqual(problems, [])
        self.assertEqual(layout, [])
        self.assertEqual(unread, [])

    def test_a_field_accounts_for_the_name_javascript_uses(self):
        layout, _, problems = scrape_props.account(
            {"borderRadius"},
            struct([{"name": "borderRadii", "javascript": "borderRadius"}]),
            [],
            [],
        )
        self.assertEqual(problems, [])
        self.assertEqual(layout, [])

    def test_a_field_accounts_for_its_other_spellings(self):
        layout, _, problems = scrape_props.account(
            {"borderRadius", "borderTopLeftRadius"},
            struct(
                [
                    {
                        "name": "borderRadii",
                        "javascript": "borderRadius",
                        "spellings": ["borderTopLeftRadius"],
                    }
                ]
            ),
            [],
            [],
        )
        self.assertEqual(problems, [])
        self.assertEqual(layout, [])

    def test_a_name_in_the_layout_type_becomes_yogas(self):
        layout, _, problems = scrape_props.account({"padding"}, struct([]), ["padding"], [])
        self.assertEqual(problems, [])
        self.assertEqual(layout, ["padding"])

    def test_a_layout_name_a_struct_declares_is_not_counted_twice(self):
        # `zIndex` and the border widths are in the layout type as well, because
        # Yoga needs them to lay out, and both are fields a host reads. They
        # belong to the struct's row rather than to Yoga's section.
        layout, _, problems = scrape_props.account(
            {"zIndex", "padding"}, struct([{"name": "zIndex"}]), ["zIndex", "padding"], []
        )
        self.assertEqual(problems, [])
        self.assertEqual(layout, ["padding"])

    def test_a_name_nothing_reads_is_accounted_for_by_being_listed(self):
        layout, unread, problems = scrape_props.account(
            {"userSelect"}, struct([]), [], ["userSelect"]
        )
        self.assertEqual(problems, [])
        self.assertEqual(layout, [])
        self.assertEqual(unread, ["userSelect"])

    # The one this file exists for.
    def test_a_style_name_nothing_accounts_for_is_a_problem(self):
        _, _, problems = scrape_props.account({"padding"}, struct([]), [], [])
        self.assertEqual(len(problems), 1)
        self.assertIn("padding", problems[0])
        self.assertIn("nothing here accounts for", problems[0])

    def test_a_name_listed_as_unread_that_upstream_dropped_is_a_problem(self):
        _, unread, problems = scrape_props.account(set(), struct([]), [], ["goneAway"])
        self.assertEqual(len(problems), 1)
        self.assertIn("no longer declares", problems[0])
        self.assertEqual(unread, [])

    def test_a_name_listed_as_unread_that_something_now_reads_is_a_problem(self):
        # The happy direction of the same check: somebody implements the prop,
        # the field appears, and the list that said nothing read it has to stop
        # saying so.
        _, _, problems = scrape_props.account(
            {"userSelect"}, struct([{"name": "userSelect"}]), [], ["userSelect"]
        )
        self.assertEqual(len(problems), 1)
        self.assertIn("something here now reads it", problems[0])


class Parsing(unittest.TestCase):
    def test_a_field_region_is_read_a_line_at_a_time(self):
        header = """
class Thing {
 public:
#pragma mark - Fields

  int count{};
  Float opacity{std::numeric_limits<Float>::quiet_NaN()};
  std::optional<FontWeight> fontWeight{};

#pragma mark - Other
  int notAField{};
};
"""
        names, unreadable = scrape_props.scrape(header, "Fields")
        self.assertEqual(names, ["count", "opacity", "fontWeight"])
        self.assertEqual(unreadable, [])

    def test_an_operator_declaration_is_not_a_field(self):
        # `ParagraphAttributes` declares `operator==` inside its field region,
        # and the field regex reads that as a field called `operator`: its
        # `==(...)` matches the "or an initialiser" branch, which has to accept
        # anything at all. It came out as a row with no status, which is the
        # check doing its job and the wrong fix.
        header = """
class Thing {
 public:
#pragma mark - Fields

  int count{};
  bool operator==(const Thing &rhs) const;
};
"""
        names, unreadable = scrape_props.scrape(header, "Fields")
        self.assertEqual(names, ["count"])
        self.assertEqual(unreadable, [])

    def test_a_flow_type_is_read_without_its_nested_members(self):
        # `shadowOffset` is a nested `{width, height}`, and a flat scan reports
        # those two as style props of their own.
        text = """
export type ____ShadowStyle_InternalCore = $ReadOnly<{
  shadowColor?: ____ColorValue_Internal,
  shadowOffset?: $ReadOnly<{
    width?: number,
    height?: number,
  }>,
  shadowOpacity?: number,
}>;
"""
        members = scrape_props.flow_members(text, "____ShadowStyle_InternalCore")
        self.assertEqual(members, ["shadowColor", "shadowOffset", "shadowOpacity"])

    def test_a_flow_type_that_is_gone_is_not_an_empty_one(self):
        self.assertIsNone(scrape_props.flow_members("type Other = {};", "____Renamed"))


if __name__ == "__main__":
    unittest.main(argv=[sys.argv[0], "-v"])
