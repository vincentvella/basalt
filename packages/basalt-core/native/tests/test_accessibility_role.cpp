// Which of the two role props a view gets, decided once.
//
// `core/AccessibilityRole.h` exists for the reason `core/TextAlignments.h`
// does: three hosts were answering a question locally, all three the same way,
// and all three wrong. Every one read `accessibilityRole` and none read
// `role`, so `role="button"` -- which is what reactnative.dev's accessibility
// page tells an app to write -- reached every desktop as a generic view.
//
// Here rather than in a host suite because the decision is not a host's. What
// each host does with the name it is given has its own tests: the GTK suite
// asserts a GtkAccessibleRole and the AppKit one an NSAccessibilityRole.

#include "TestHarness.h"

#include "AccessibilityRole.h"

#include <optional>
#include <sstream>
#include <string>

using facebook::react::AccessibilityProps;
using facebook::react::Role;

TEST(accessibility_role_prefers_the_aria_spelling) {
  AccessibilityProps props;
  props.role = Role::Button;
  EXPECT_EQ(basalt::accessibilityRoleName(props), std::string("button"));
}

TEST(accessibility_role_falls_back_to_the_older_prop) {
  AccessibilityProps props;
  props.accessibilityRole = "adjustable";
  EXPECT_EQ(basalt::accessibilityRoleName(props), std::string("adjustable"));
}

TEST(accessibility_role_lets_the_aria_one_win) {
  // React Native's own order: `AccessibilityProps.cpp` ends with
  // `deriveAccessibilityTraits(role, accessibilityRole, state)`, which takes
  // the role first and only falls through when it is None. A view that sets
  // both is unusual; one that sets `role` and is told `accessibilityRole`'s
  // answer would be wrong.
  AccessibilityProps props;
  props.accessibilityRole = "image";
  props.role = Role::Checkbox;
  EXPECT_EQ(basalt::accessibilityRoleName(props), std::string("checkbox"));
}

TEST(accessibility_role_is_empty_when_a_view_asks_for_neither) {
  // Empty rather than a guess. Two hosts infer a role from the component name
  // when nothing was asked for -- a Paragraph is text, an Image is an image --
  // and that inference belongs with the host that makes it, not here.
  AccessibilityProps props;
  EXPECT(basalt::accessibilityRoleName(props).empty());
}

// --- A paragraph's fragments ------------------------------------------------
//
// `TextAttributes` carries the same two props per run, because a <Text> inside
// a <Text> is a run rather than a view. A paragraph every fragment of which
// agrees is that role; anything mixed needs an accessible child per run, which
// is a component and not a prop. See the header.

namespace {

facebook::react::AttributedString::Fragment fragmentWith(const char *text,
                                                         std::optional<Role> role,
                                                         std::optional<facebook::react::AccessibilityRole> older) {
  facebook::react::TextAttributes attributes;
  attributes.role = role;
  attributes.accessibilityRole = older;

  facebook::react::AttributedString::Fragment fragment;
  fragment.string = text;
  fragment.textAttributes = attributes;
  return fragment;
}

facebook::react::AttributedString stringOf(
    std::initializer_list<facebook::react::AttributedString::Fragment> fragments) {
  facebook::react::AttributedString string;
  for (auto fragment : fragments) {
    string.appendFragment(std::move(fragment));
  }
  return string;
}

} // namespace

TEST(fragment_role_takes_the_role_one_run_asks_for) {
  // The shape that reaches here: <Text><Text role="header">Hi</Text></Text>,
  // a nested Text being the only way a role lands on a fragment and not on the
  // paragraph.
  EXPECT_EQ(basalt::fragmentRoleName(stringOf({fragmentWith("Hi", Role::Heading, {})})),
            std::string("heading"));
}

TEST(fragment_role_prefers_the_aria_spelling_here_too) {
  EXPECT_EQ(basalt::fragmentRoleName(
                stringOf({fragmentWith("Hi", Role::Heading, facebook::react::AccessibilityRole::Link)})),
            std::string("heading"));
  // And falls back to the older prop, which is the rule one layer up.
  EXPECT_EQ(basalt::fragmentRoleName(
                stringOf({fragmentWith("Hi", {}, facebook::react::AccessibilityRole::Link)})),
            std::string("link"));
}

TEST(fragment_role_needs_every_run_to_agree) {
  EXPECT_EQ(basalt::fragmentRoleName(stringOf({fragmentWith("Hi ", Role::Heading, {}),
                                               fragmentWith("there", Role::Heading, {})})),
            std::string("heading"));
  // Two roles is a paragraph that needs sub-elements, and gets nothing here.
  EXPECT_EQ(basalt::fragmentRoleName(stringOf({fragmentWith("Hi ", Role::Heading, {}),
                                               fragmentWith("there", Role::Link, {})})),
            std::string(""));
}

// A run with no role vetoes, and this is the case that matters: "Hello
// <Text role="link">here</Text>" is two fragments, and a paragraph announced
// wholly as a link because its last word is one is worse than one announced as
// text.
TEST(fragment_role_is_refused_when_one_run_asks_for_nothing) {
  EXPECT_EQ(basalt::fragmentRoleName(stringOf({fragmentWith("Hello ", {}, {}),
                                               fragmentWith("here", Role::Link, {})})),
            std::string(""));
  EXPECT_EQ(basalt::fragmentRoleName(stringOf({fragmentWith("Hi", {}, {})})), std::string(""));
}

TEST(fragment_role_of_no_text_is_nothing) {
  EXPECT_EQ(basalt::fragmentRoleName(facebook::react::AttributedString{}), std::string(""));
}
