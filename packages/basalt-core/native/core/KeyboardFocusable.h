// Whether Tab stops on a view.
//
// Three hosts asked this question and all three answered it with the same
// expression, written out three times:
//
//     (props->accessible || isControl) && !props->accessibilityElementsHidden
//
// Which was fine while it was two conditions, and stopped being fine when a
// third arrived: `accessibilityRespondsToUserInteraction`, which no host read.
// Adding it in three places is how two of them end up disagreeing, and that is
// the bug `core/AccessibilityRole.h` and `core/TextAlignments.h` exist about.
//
// ## Why `accessible` is the signal and not `focusable`
//
// React Native has a `focusable` prop and it does not reach this platform:
// ReactCommon parses it into Android's props and tvOS's, and a desktop host
// gets neither. `accessible` is what is left, and it is what `<Pressable>`
// sets on everything it renders, so it is the honest signal for "the app
// means this to be operated".
//
// A control is focusable whether or not the app said `accessible`. `<Switch>`
// does not set it: on a phone the native control is focusable by being a
// control and React Native never had to say so, and on a desktop a switch that
// Tab skips is simply broken. A *disabled* control is excluded, which is what
// every desktop does -- Tab skips a control that cannot be operated rather
// than stopping on one that does nothing -- and the caller decides that, since
// it is the one holding the control state.
//
// ## `accessibilityRespondsToUserInteraction`
//
// React Native documents it as "blocks the user from interacting with the
// component through keyboard while still allowing screen reader to interact
// with it if this View is still accessible", and marks it `@platform ios`. The
// two halves of that sentence are what makes it a desktop prop rather than an
// iOS one: the keyboard is a desktop's primary input, and "not keyboard
// reachable but still in the accessibility tree" is a state a desktop can
// express exactly. iOS reaches it through Full Keyboard Access; here it is the
// Tab order, and leaving the view accessible is the other half -- which is why
// this gates focus only and nothing else reads the prop.
//
// It defaults to true in ReactCommon, so a view that says nothing is unchanged
// by it.
//
// ## Hidden
//
// A hidden view is not focusable whatever else it says. Tab stopping on
// something nobody can see is worse than Tab skipping it.

#pragma once

#include <react/renderer/components/view/AccessibilityProps.h>

namespace basalt {

// Whether the keyboard's focus chain should include this view.
//
// `isControl` is the host's answer to "this is a control the platform draws",
// already excluding a disabled one; see DesktopControls.h.
inline bool keyboardFocusable(const facebook::react::AccessibilityProps &props, bool isControl) {
  if (!props.accessibilityRespondsToUserInteraction) {
    return false;
  }
  return (props.accessible || isControl) && !props.accessibilityElementsHidden;
}

} // namespace basalt
