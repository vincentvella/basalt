// Animation frames, on Windows.
//
// React Native's `AnimationChoreographer` is asked to call back once per frame
// while something is animating, and to stop when nothing is. macOS gets that
// from a CADisplayLink and GTK from the widget's frame clock -- both of which
// are told by the compositor when a frame is due.
//
// Win32 has no such callback for an ordinary window. The honest options are a
// timer, or `DwmGetCompositionTimingInfo` plus `DwmFlush` on a thread that
// blocks until the compositor is ready. The second is genuinely display-locked
// and is what a serious animation implementation should use; it also needs its
// own thread, because DwmFlush blocks, and a blocked UI thread is worse than a
// slightly wrong frame interval.
//
// So this is a multimedia timer at sixty hertz, and it is worth being clear
// that this is a placeholder rather than a design. `docs/BACKLOG.md` already
// records that worklets and Reanimated run on a sixteen-millisecond timer on
// *both* other desktops for a related reason -- each platform's display link
// belongs to React Native's own choreographer and pauses when React Native has
// no animation of its own -- so this is a third instance of the same gap rather
// than a new one.
//
// What it does get right is the pausing. A timer that runs whether or not
// anything is animating wakes the process sixty times a second forever, which
// is the objection `docs/DECISIONS.md` raises against
// `gtk_widget_add_tick_callback` and which matters more on a laptop than the
// frame interval does.

#pragma once

#include <react/renderer/animationbackend/AnimationChoreographer.h>

#include <atomic>
#include <cstdint>

namespace basalt {

class Win32AnimationChoreographer final : public facebook::react::AnimationChoreographer {
 public:
  Win32AnimationChoreographer() = default;
  ~Win32AnimationChoreographer() override;

  void resume() override;
  void pause() override;

  // Stops the timer for good. Called on the way out, before the manager this
  // feeds is released.
  void detach();

  // Called from the timer, on the UI thread.
  void onFrame();

 private:
  std::atomic<bool> running_{false};
  // A UINT_PTR from SetTimer, held as an integer so this header needs no
  // windows.h -- which would otherwise reach every translation unit that
  // constructs a host.
  std::uintptr_t timer_{0};
};

} // namespace basalt
