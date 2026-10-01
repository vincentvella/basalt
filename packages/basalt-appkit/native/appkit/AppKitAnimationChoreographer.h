// Drives React Native's animation frame callbacks from the display's refresh.
//
// AnimationChoreographer is the frame-callback seam in ReactCxxPlatform. It is
// NOT what drives mounting -- mounts arrive on the JS thread via the
// RuntimeScheduler and are marshalled by AppKitMountingManager. This is purely the
// "there is a new frame" signal the animation backend needs, and the counterpart
// of gtk/GtkAnimationChoreographer, which rides GTK's frame clock.

#pragma once

#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#endif

#include <react/renderer/animationbackend/AnimationChoreographer.h>

#include <atomic>

namespace basalt {

class AppKitAnimationChoreographer final : public facebook::react::AnimationChoreographer {
 public:
  AppKitAnimationChoreographer() = default;
  ~AppKitAnimationChoreographer() override;

  // Called from the JS thread. Both marshal to the main thread before touching
  // the display link.
  void resume() override;
  void pause() override;

#ifdef __OBJC__
  // Called on the main thread once the view is in a window: a display link is
  // tied to the screen the view is on, and a view with no window has no screen.
  // The same reason the GTK side waits for realisation.
  void attachToView(NSView *view);
#endif
  void detach();

  // Not private: the trampoline object that receives the display link callback
  // has to reach it, and it is not something a caller would ever want.
  void onFrame(double timestampSeconds);

 private:
  void syncRunningState();

  // Opaque to C++ callers. Holds the CADisplayLink and its target.
  void *link_{nullptr};
  void *target_{nullptr};

  // Written from the JS thread, read on the main thread.
  std::atomic<bool> running_{false};
};

} // namespace basalt
