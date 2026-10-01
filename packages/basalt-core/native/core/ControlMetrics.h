// How big a control is, on every desktop.
//
// Separate from DesktopControls.h, which is where everything else about a
// control lives, for one reason: the Windows view layer draws its own switch
// and its own spinner and deliberately does not link React Native -- the demo
// and half its tests build a view tree with no Fabric anywhere -- so it cannot
// include a header that pulls in ShadowView. It can include this, which is four
// numbers and nothing else. FocusRing.h is here for the same reason.
//
// Constants rather than each toolkit's intrinsic size, so that a layout built
// against one desktop is the same on the other two. A GtkSwitch and an NSSwitch
// are both smaller than this, and a screen that fits on a Mac and reflows on
// Linux is the failure this project exists to avoid.

#pragma once

namespace basalt {

// React Native's <Switch> size on iOS, which is what app layouts are written
// against -- and what SwitchShadowNode::measureContent answers, since React
// Native's only <Switch> shadow node measures by instantiating a UISwitch.
inline constexpr float kSwitchWidth = 51.0F;
inline constexpr float kSwitchHeight = 31.0F;

// <ActivityIndicator>'s two sizes. React Native's JavaScript also writes these
// into the view's style, so these are for the drawing rather than the layout --
// which is exactly why they have to agree with it.
inline constexpr float kSpinnerSmall = 20.0F;
inline constexpr float kSpinnerLarge = 36.0F;

// React DevTools' overlay outline, in points. The same on every platform so
// that a screenshot of an inspected element is the same picture from any of
// them. Here rather than in DebuggingOverlay.h for the reason this file exists:
// a view layer draws it and does not link React Native.
inline constexpr float kHighlightBorderWidth = 2.0F;

} // namespace basalt
