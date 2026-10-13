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
