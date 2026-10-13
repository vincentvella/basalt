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

#include <react/renderer/attributedstring/AttributedString.h>
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

// --- A paragraph's fragments ------------------------------------------------
//
// The same two props again, one layer down: `TextAttributes` carries a `role`
// and an `accessibilityRole` per *fragment*, because a `<Text>` inside a
// `<Text>` is a run of the same paragraph rather than a view of its own. Three
// hosts read neither.
//
// Honouring them properly needs sub-element accessibility -- an accessible
// child per run, with a frame -- which is what iOS does:
// `RCTParagraphComponentAccessibilityProvider` builds a `UIAccessibilityElement`
// for each, positioned from the layout manager. That is a component rather than
// a prop, and only one of these three hosts could have it.
//
// What is answerable now is the case where no sub-elements are needed: a
// paragraph every fragment of which asks for the same role *is* that role, and
// the view already has somewhere to put it. `<Text><Text role="header">Hi
// </Text></Text>` is the shape that reaches here, a nested Text being the only
// way a role arrives on a fragment and not on the paragraph.
//
// ## Why every fragment and not the first
//
// Because the alternative is wrong out loud. `core/TextShadows.h` lets the
// first fragment that asks decide the paragraph's shadow, which is fine for a
// shadow: the worst case is the wrong blur. A role is announced. "Hello
// <Text role="link">here</Text>" has two fragments and one of them is a link,
// and a paragraph announced wholly as a link because its last word is one
// would be worse than one announced as text -- the same rule each host's role
// table already follows in refusing to guess.
//
// So a fragment with no role vetoes too. Two fragments, one a header and one
// plain, is a paragraph that needs sub-elements and gets nothing here.
inline std::string fragmentRoleName(const facebook::react::AttributedString &text) {
  const auto &fragments = text.getFragments();
  if (fragments.empty()) {
    return {};
  }
  std::string agreed;
  for (size_t i = 0; i < fragments.size(); i++) {
    const auto &attributes = fragments[i].textAttributes;
    // `role` first, which is React Native's own order; see above.
    std::string name;
    if (attributes.role.has_value()) {
      name = facebook::react::toString(*attributes.role);
    } else if (attributes.accessibilityRole.has_value()) {
      name = facebook::react::toString(*attributes.accessibilityRole);
    }
    if (name.empty()) {
      return {};
    }
    if (i == 0) {
      agreed = name;
    } else if (name != agreed) {
      return {};
    }
  }
  return agreed;
}

} // namespace basalt
