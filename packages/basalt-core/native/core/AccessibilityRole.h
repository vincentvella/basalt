// Which role a view claims, when it can say so two ways.
//
// React Native has `accessibilityRole`, which is its own vocabulary and
// arrived first, and `role`, which is ARIA's and is what the documentation now
// tells an app to write. ReactCommon parses both -- `accessibilityRole` into a
// string and `role` into a `Role` enum -- and hands a host both, side by side,
// having decided nothing between them.
//
// Deciding is this file. All three hosts read `accessibilityRole` and none
// read `role`, which is the same shape of bug `core/TextAlignments.h` was
// written about: three hosts answering a question locally and a prop arriving
// at none of them. An app that writes `role="button"` -- which is what
// reactnative.dev's accessibility page shows -- gets a generic view on every
// desktop.
//
// ## Which wins
//
// `role` does, and that is React Native's own order rather than a preference.
// `AccessibilityProps.cpp` ends its constructor with
// `deriveAccessibilityTraits(role, accessibilityRole, accessibilityState)`,
// and that function takes `role` first and only falls to `accessibilityRole`
// when the role is `None`. A view setting both is unusual; a view setting
// `role` and getting `accessibilityRole`'s answer would be wrong.
//
// ## One vocabulary, nearly
//
// `toString(Role)` gives ARIA's spelling -- "button", "alert", "checkbox" --
// and each host already maps React Native's, which GtkMountingManager's own
// comment calls "mostly ARIA's". So the two meet in the same table, and a name
// only one of them has falls through that table's existing default: GENERIC on
// GTK, no role on the others. That is deliberate and is the rule those tables
// were already written to -- a wrong role is worse for a screen reader than no
// role, because it makes the widget announce itself as something it is not.

#pragma once

#include <react/renderer/components/view/AccessibilityProps.h>
#include <react/renderer/components/view/accessibilityPropsConversions.h>

#include <string>

namespace basalt {

// The role name a view asks for, preferring the ARIA `role` over the older
// `accessibilityRole`, and empty when it asks for neither.
//
// Empty rather than a guess, because what a host does with "nothing asked for"
// differs: two of them infer a role from the component name, and that
// inference belongs with the host that makes it.
inline std::string accessibilityRoleName(const facebook::react::AccessibilityProps &props) {
  if (props.role != facebook::react::Role::None) {
    return facebook::react::toString(props.role);
  }
  return props.accessibilityRole;
}

} // namespace basalt
