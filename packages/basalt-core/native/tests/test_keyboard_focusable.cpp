// Whether Tab stops on a view, decided once.
//
// `core/KeyboardFocusable.h` exists because three hosts had written out the
// same two-condition expression and a third condition had arrived that none of
// them read: `accessibilityRespondsToUserInteraction`, which React Native
// documents as blocking keyboard interaction while leaving the view in the
// accessibility tree.
//
// Here rather than in a host suite because the decision is not a host's. What
// each host does with the answer has its own tests: the GTK suite asserts
// `gtk_widget_set_focusable`, the Win32 one the flag in its tree dump, and the
// AppKit one `acceptsFirstResponder`.

#include "TestHarness.h"

#include "KeyboardFocusable.h"

#include <sstream>

using facebook::react::AccessibilityProps;

TEST(keyboard_focusable_an_accessible_view_is_focusable) {
  AccessibilityProps props;
  props.accessible = true;
  EXPECT(basalt::keyboardFocusable(props, false));
}

TEST(keyboard_focusable_a_plain_view_is_not) {
  AccessibilityProps props;
  EXPECT(!basalt::keyboardFocusable(props, false));
}

// `<Switch>` never says `accessible`: on a phone the native control is
// focusable by being a control. On a desktop a switch Tab skips is broken.
TEST(keyboard_focusable_a_control_is_focusable_without_asking) {
  AccessibilityProps props;
  EXPECT(basalt::keyboardFocusable(props, true));
}

// Tab stopping on something nobody can see is worse than Tab skipping it.
TEST(keyboard_focusable_a_hidden_view_is_not_focusable) {
  AccessibilityProps props;
  props.accessible = true;
  props.accessibilityElementsHidden = true;
  EXPECT(!basalt::keyboardFocusable(props, false));
  // Including a control, which is the case the host expressions got right only
  // because the `&&` bound the whole thing.
  EXPECT(!basalt::keyboardFocusable(props, true));
}

// `accessibilityRespondsToUserInteraction`, the prop this file was written
// for. False blocks the keyboard and nothing else: the view stays accessible,
// which is the other half of React Native's own sentence about it.
TEST(keyboard_focusable_a_view_that_refuses_interaction_is_not_focusable) {
  AccessibilityProps props;
  props.accessible = true;
  props.accessibilityRespondsToUserInteraction = false;
  EXPECT(!basalt::keyboardFocusable(props, false));
  // A control too. The prop is the app's decision and being a control is the
  // platform's, and the app's wins -- it is the one that said so.
  EXPECT(!basalt::keyboardFocusable(props, true));
}

// It defaults to true in ReactCommon, so a view that says nothing about it is
// unchanged. Asserted rather than assumed: a default that flipped upstream
// would otherwise make every view on three desktops unreachable by Tab.
TEST(keyboard_focusable_responding_is_the_default) {
  AccessibilityProps props;
  EXPECT(props.accessibilityRespondsToUserInteraction);
  props.accessible = true;
  EXPECT(basalt::keyboardFocusable(props, false));
}
