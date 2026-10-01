// Which mouse button, and what the three desktops do about it.
//
// Header-only and shared, because the numbers are the thing three copies drift
// on: each toolkit numbers its buttons differently, W3C numbers them a third
// way, and React Native's `PointerEvent` carries W3C's.
//
// ## Why this exists at all
//
// React Native's *touch* model has no concept of which button -- a phone has
// one finger and no buttons -- so until now every host forwarded every click as
// a touch. That was three different wrongs. GTK set its click gesture to button
// 0, meaning all of them, and AppKit forwarded `rightMouseDown:` the same as
// `mouseDown:`, so on both a right-click *activated* whatever it landed on: a
// `<Pressable>` fired its `onPress` from the wrong button. Windows handled only
// WM_LBUTTONDOWN, so a right-click there did nothing whatsoever.
//
// What a desktop expects is neither. A secondary click is not a press -- the web
// does not fire `click` for one, and no desktop toolkit treats it as an
// activation -- it is a request for a context menu. So a secondary or middle
// click now produces a pointer event and no touch, which is what lets an app
// answer it without every `<Pressable>` on the screen answering first.

#pragma once

#include <cstdint>

namespace basalt {

// W3C's numbering, which is what `PointerEvent::button` carries. Not the
// toolkits': GTK counts from 1, AppKit has a separate selector per button, and
// Windows has a separate message.
enum class PointerButton : int {
  Primary = 0,
  Middle = 1,
  Secondary = 2,
};

// Whether this button presses things.
//
// The whole question in one place. Only the primary button drives React
// Native's touch model -- responder negotiation, `onPress`, a gesture handler
// claiming the pointer -- because that model has no way to say which button it
// was, and a right-click that pressed things is the bug this exists to stop.
inline bool isPressButton(PointerButton button) {
  return button == PointerButton::Primary;
}

// `PointerEvent::buttons`, the mask of what is held *during* the event. A
// different number from `button` in the same event, which is exactly the sort
// of thing worth writing down once: primary is 1, secondary is 2, middle is 4.
inline std::uint32_t buttonsMaskFor(PointerButton button) {
  switch (button) {
    case PointerButton::Primary:
      return 1;
    case PointerButton::Middle:
      return 4;
    case PointerButton::Secondary:
      return 2;
  }
  return 0;
}

} // namespace basalt
