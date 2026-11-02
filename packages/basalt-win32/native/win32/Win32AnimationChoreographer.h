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
// So this is a timer at sixty hertz, and it is worth being clear that this is
// a placeholder rather than a design. `docs/BACKLOG.md` already
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

#include "DisplayChoreographer.h"

#include <cstdint>
#include <mutex>

namespace basalt {

// Also what paces requestAnimationFrame here; core/DisplayChoreographer.h
// decides when the timer runs.
class Win32AnimationChoreographer final : public DisplayChoreographer {
 public:
  Win32AnimationChoreographer() = default;
  ~Win32AnimationChoreographer() override;

  // Stops the timer for good. Called on the way out, before the manager this
  // feeds is released.
  void detach();

  // Called from the timer, on the UI thread.
  void onFrame();

 protected:
  // Starts or stops the timer to match running(). Any thread.
  void runningChanged() override;

 private:
  // runningChanged() can arrive from the JS thread and detach() from the UI
  // thread; the timer id is the one thing they share.
  std::mutex timerMutex_;
  // A `basalt::UiTimerId` rather than a `SetTimer` handle, and not for
  // tidiness: WM_TIMER ranks below WM_PAINT, so on this timer an app that
  // repaints continuously -- anything with a `<Canvas>` -- stopped getting
  // animation frames at all. See win32/Win32UiTimer.h. Held as a plain
  // integer so this header needs no windows.h, which would otherwise reach
  // every translation unit that constructs a host.
  std::uint64_t timer_{0};
};

} // namespace basalt
