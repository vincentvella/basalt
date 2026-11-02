// One frame clock per host, shared by the two things that want frames.
//
// React Native's animation backend asks for frames through
// AnimationChoreographer's resume() and pause(). requestAnimationFrame wants
// them too -- React Native's own is setTimeout(0), which a loop that
// re-requests itself turns into a spin (see core/AnimationFrames.h) -- and it
// should get the same display-paced tick rather than a second clock.
//
// So this owns the two demands and the answer to "should the clock run", and a
// host only starts and stops its clock (a CADisplayLink, GTK's frame clock, a
// UI timer on Windows) when runningChanged() says to, and calls deliverFrame()
// on each tick. Nothing ticks while neither wants frames, which is what keeps an
// idle app at zero CPU.

#pragma once

#include <react/renderer/animationbackend/AnimationChoreographer.h>

#include <atomic>
#include <functional>
#include <mutex>

namespace basalt {

class DisplayChoreographer : public facebook::react::AnimationChoreographer {
 public:
  // The animation backend's half. Called from the JavaScript thread.
  void resume() final;
  void pause() final;

  // requestAnimationFrame's half: frames while a callback is waiting for one.
  // Any thread.
  void setFramesWanted(bool wanted);

  // What a tick does for requestAnimationFrame. Called on the host's UI thread,
  // once per frame while frames are wanted. Replaceable: a reload installs a new
  // one for the new runtime.
  void setFrameListener(std::function<void()> listener);

  // Whether either half wants frames now.
  bool running() const;

 protected:
  // running() may have changed. Called on whichever thread changed it; a host
  // moves to its UI thread before touching its clock.
  virtual void runningChanged() = 0;

  // A host's clock ticked, on its UI thread.
  void deliverFrame(facebook::react::AnimationTimestamp timestamp);

 private:
  static constexpr unsigned kBackend = 1;
  static constexpr unsigned kFrames = 2;

  void setWant(unsigned bit, bool on);

  std::atomic<unsigned> wants_{0};
  std::mutex listenerMutex_;
  std::function<void()> listener_;
};

} // namespace basalt
