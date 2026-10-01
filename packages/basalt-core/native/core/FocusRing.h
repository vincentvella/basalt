// What keyboard focus looks like.
//
// Two numbers and a colour, in one place, for the same reason
// `kWheelStepPixels` is: a ring that is two pixels of blue on one desktop and
// three of grey on another is a difference an app did not ask for and cannot
// control. Matching the other platform matters more here than matching either
// toolkit's own default, which is the argument of this project in one constant.
//
// Not the system accent colour, on either desktop, and that is a real choice
// rather than an omission. macOS has `NSColor.keyboardFocusIndicatorColor` and
// GTK has a theme; using each would make the ring native and make the two
// desktops disagree, and an app that draws its own buttons in its own colours
// has already said it is not trying to look native. The same reasoning kept
// `<TextInput>`'s peers looking like the app rather than like the toolkit.
//
// Drawn rather than delegated for a second reason too: these widgets have no
// theme to delegate to. React Native decided every colour in the tree, and GTK's
// focus rendering is a CSS outline on a themed widget.

#pragma once

namespace basalt {

// Pixels, outside the view's own bounds, so the ring does not cover content at
// the edge of a button.
constexpr float kFocusRingWidth = 2.0F;

// A blue that reads as "focused" on a light background and on a dark one, which
// is what an app-coloured tree offers no guarantees about. sRGB, 0..1 per
// channel, in the order every host's colour type takes.
constexpr float kFocusRingRed = 0.259F;
constexpr float kFocusRingGreen = 0.522F;
constexpr float kFocusRingBlue = 0.957F;
constexpr float kFocusRingAlpha = 1.0F;

} // namespace basalt
