// Hover, which is the one piece of input a desktop has and a phone does not.
//
// React Native's touch events -- what Pressability and every `onPress` run on
// -- have no concept of a pointer that is present but not pressed. W3C pointer
// events do, and React Native carries them alongside touches: `onPointerEnter`,
// `onPointerLeave`, `onPointerOver`, `onPointerOut` and `onPointerMove` are
// real props, registered in the view config and parsed into `ViewProps::events`
// by ReactCommon. Nothing on this platform was emitting them.
//
// ## What a platform owes, and what it must not do
//
// Much less than it looks. `PointerEventsProcessor` in ReactCommon already
// implements the whole hover model: it remembers the path the cursor was on,
// diffs it against the new one, and emits `out`, `leave`, `over` and `enter` in
// the right order with the right listener filtering, all from a single
// `pointerMove`. It also filters the move itself by whether anything on the
// path is listening. So a host emits *moves* and nothing else, and every other
// pointer event an app sees is React Native's own work.
//
// This is worth stating because emitting enter and leave as well looks right
// and is wrong twice over: the processor runs its hover tracking on every
// pointer event that reaches it, so each extra one produces a second complete
// round of enter/leave, and the platform's own is then forwarded on top. One
// `over` from here came out in JavaScript as `over`, `enter` on the ancestor
// and `enter` on the target -- correct events, three times too many.
//
// The one exception is the cursor leaving the surface, which no move can
// express: a `pointerLeave` from a platform is read by the processor as "the
// pointer is gone", and it unwinds the whole path itself rather than forwarding
// what it was sent.
//
// ## So what is left here
//
// A gate. A cursor crossing a window produces motion at the refresh rate, and
// turning each one into an event that crosses into JavaScript, for an app that
// listens to none of them, is a cost every app would pay. React Native on
// Android has the same problem and answers it the same way -- ask whether
// anyone is listening before dispatching -- and the answer is in
// `ViewProps::events`, which the mounting walk already sees on every Create and
// Update.
//
// The gate has one subtlety, and it is the reason this is a small state machine
// rather than an `if`. A move must also be dispatched on the *first* motion
// after the cursor leaves everything that was listening: without it the
// processor never learns the pointer left, and the last hovered view keeps its
// hover state for good.

#pragma once

#include <react/renderer/components/view/primitives.h>

#include <cstdint>

namespace basalt {

// Which hover events a view listens for. A subset of React Native's
// `ViewEvents`, narrowed to the bits this file acts on so that a host can hold
// one small integer per view instead of a 64-bit set it would have to
// understand.
enum HoverListener : std::uint16_t {
  HoverListenerEnter = 1U << 0U,
  HoverListenerLeave = 1U << 1U,
  HoverListenerMove = 1U << 2U,
  HoverListenerOver = 1U << 3U,
  HoverListenerOut = 1U << 4U,
  // The capture phase of each. A listener in the capture phase is on an
  // *ancestor* of the view the event is dispatched to, so it counts towards
  // whether anything on the path is listening.
  HoverListenerEnterCapture = 1U << 5U,
  HoverListenerLeaveCapture = 1U << 6U,
  HoverListenerMoveCapture = 1U << 7U,
  HoverListenerOverCapture = 1U << 8U,
  HoverListenerOutCapture = 1U << 9U,

  // Nothing here is listened for, which is the common case and the one worth
  // being able to test for in a single comparison.
  HoverListenerNone = 0U,
};

// The mask for a view, from the props ReactCommon parsed. `ViewEvents` is a
// 64-bit set with a documented bit per prop; this keeps the translation in one
// place so no host has to know the offsets.
std::uint16_t hoverListenersFrom(const facebook::react::ViewEvents &events);

class HoverTracker {
 public:
  // The cursor is over `tag`, and `listenersOnPath` is the masks of every view
  // from it to the surface root, ORed together. Returns whether a
  // `pointerMove` should be dispatched at `tag`.
  bool admitMove(int tag, std::uint16_t listenersOnPath);

  // The cursor left the surface. Returns the tag to dispatch a `pointerLeave`
  // at, or 0 for nothing -- which is the case when no move was ever dispatched,
  // because the processor has no path to unwind.
  int admitLeave();

  // Forget where the cursor was without emitting anything, for a surface
  // teardown: the views in the remembered path are about to stop existing.
  void forget();

 private:
  // The view the last dispatched move was reported against, or 0 if the last
  // motion was not dispatched. Not simply "the last view under the cursor":
  // what matters is what React Native was told, because that is what it is
  // holding a hover path for.
  int emittedAt_{0};
};

} // namespace basalt
