// react-native-gesture-handler's recognisers, on the desktop.
//
// RNGH is not an Expo module and has no portable C++ to compile: it ships
// Objective-C for Apple, Kotlin for Android, and a TypeScript engine for the
// web. What is portable is its *contract*, which is small and well defined --
// eight module methods, a six-value state machine, and two device events -- and
// that contract is what this file implements.
//
// The division is the same one the rest of this project uses. Everything here
// is platform-independent: a handler is a state machine fed with a pointer
// position and a list of the views under it, and what it produces is a payload
// to hand to JavaScript. The toolkits contribute two things and no more --
// where the pointer is, and which views are under it -- which is exactly what
// each one's touch dispatcher already works out for React Native's own touches.
//
// ## What a handler is
//
// `createGestureHandler('PanGestureHandler', 7, config)` makes one.
// `attachGestureHandler(7, viewTag)` says which view it watches. From then on
// every pointer event that lands on that view, or anything inside it, is
// offered to the handler, and the handler decides whether this is the gesture
// it is looking for.
//
// The states are RNGH's, and the order matters to its JavaScript:
// UNDETERMINED -> BEGAN when a pointer lands, BEGAN -> ACTIVE when the
// recogniser is satisfied, ACTIVE -> END when the pointer lifts. FAILED and
// CANCELLED are the two ways out.
//
// ## What is recognised, and what is not
//
// Tap, long press, pan, fling, native and manual. Pinch, rotation and force
// touch are created and never activate, because they need a second finger or a
// pressure reading that a mouse does not have; saying so here is better than a
// handler that looks attached and quietly never fires.
//
// One pointer. A desktop has one cursor, so `numberOfPointers` is always 1 and
// a gesture configured to need two never activates.
//
// ## Conflicts
//
// When a handler activates it cancels every other handler tracking the same
// pointer, unless the two were declared simultaneous. `waitFor` holds a handler
// at BEGAN until the handler it waits for has failed. That is the useful half
// of RNGH's conflict resolution; what is missing is listed in
// docs/backlog/expo.md, which is the file that changes when it stops being

#pragma once

#include <folly/dynamic.h>

#include <functional>
#include <string>
#include <vector>

namespace basalt {

// RNGH's State enum, from its State.ts. The numbers are part of the contract:
// they cross into JavaScript as-is.
enum class GestureState {
  Undetermined = 0,
  Failed = 1,
  Began = 2,
  Cancelled = 3,
  Active = 4,
  End = 5,
};

// One view under the pointer, with its origin in the surface root's
// coordinates. The origin is what turns a root-relative pointer position into
// the view-relative `x`/`y` a gesture payload carries, without this file
// knowing anything about either toolkit's geometry.
struct HitView {
  int tag{0};
  double originX{0};
  double originY{0};
};

class GestureRegistry {
 public:
  // How a payload reaches JavaScript. RNGH listens on DeviceEventEmitter for
  // `onGestureHandlerEvent` and `onGestureHandlerStateChange`, so this is set
  // by the TurboModule, which is the thing that can emit one.
  using Emitter = std::function<void(const std::string &eventName, folly::dynamic payload)>;
  void setEmitter(Emitter emitter);

  // The module's methods.
  void create(const std::string &handlerName, int handlerTag, folly::dynamic config);
  void update(int handlerTag, folly::dynamic config);
  void attach(int handlerTag, int viewTag);
  void drop(int handlerTag);

  // Input, from a platform's touch dispatcher. `chain` is the views under the
  // point, innermost first, which is the order handlers are offered the gesture
  // in. Positions are in the surface root's coordinates.
  void pointerDown(const std::vector<HitView> &chain, double x, double y, double timestampMs);
  void pointerMove(double x, double y, double timestampMs);
  void pointerUp(double x, double y, double timestampMs);
  void pointerCancel();

  // Forces a handler into a state, which is what RNGH's `GestureStateManager`
  // does from a worklet: `manager.activate()` inside `onTouchesMove` is a
  // gesture claiming itself rather than waiting for a recogniser to decide.
  // Reanimated is the only caller -- the manager needs a worklet runtime to run
  // in -- so this does nothing useful until phase 38.
  void setStateFromWorklet(int handlerTag, int state);

  // True once a handler has activated during the current gesture. A touch
  // dispatcher reads this to cancel React Native's own touch, which is what
  // `setJSResponder` does on the platforms RNGH was written for: without it a
  // pan over a <Pressable> both pans and presses.
  bool hasActiveHandler() const;

  // Whether anything is attached at all. Checked before the work of building a
  // hit chain, because an app that does not use RNGH must not pay for it.
  bool empty() const;

 private:
  struct Handler;

  void beginTracking(Handler &handler, const HitView &view, double x, double y, double timestampMs);
  void setState(Handler &handler, GestureState next, double x, double y, double timestampMs);
  bool tryActivate(Handler &handler, double x, double y, double timestampMs);
  void cancelOthers(const Handler &activated, double x, double y, double timestampMs);
  void emitUpdate(const Handler &handler, double x, double y, double timestampMs);
  folly::dynamic payloadFor(const Handler &handler, double x, double y) const;
  Handler *find(int handlerTag);

  std::vector<Handler> handlers_;
  Emitter emit_;

  // The gesture in progress. Empty between gestures.
  std::vector<int> tracking_;
  bool pointerIsDown_{false};
  int generation_{0};
};

GestureRegistry &gestures();

// A monotonic clock in milliseconds, shared so that both platforms' input
// timestamps mean the same thing -- durations and velocities are compared
// against constants from RNGH's own recognisers, which are wall-clock
// milliseconds.
double monotonicMilliseconds();

} // namespace basalt
