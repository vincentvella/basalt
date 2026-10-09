// Which frame an animated image is showing, and when the next one is due.
//
// A GIF carries a delay per frame and a loop count, and every platform hands
// those over differently: ImageIO answers `CGImageSourceGetCount` and a
// per-frame dictionary, WIC the same through its metadata reader, gdk-pixbuf
// nothing at all -- it owns the timing itself behind `GdkPixbufAnimationIter`
// and offers no indexed access to frames. So this file holds the part that is
// the same wherever the frames come from: the clamp, and the arithmetic from
// elapsed time to a frame index.
//
// The GTK host uses only the clamp, and asks gdk-pixbuf for the rest. It is
// still worth sharing, because an animation driven by the platform's own clock
// is one a test has to wait for, and a test that waits for a GIF is a test that
// is slow and flaky at once. Both hosts advance their animation from an elapsed
// time the caller supplies, which is what makes "150 milliseconds later" a
// thing a suite can say.
//
// ## The clamp, which is the browsers' rule rather than the format's
//
// A GIF may ask for a delay of zero, and a great many do: in the format's own
// terms that means "as fast as the hardware can", which in 1989 meant a
// perceptible speed and now means a thousand frames a second. Every browser
// refuses, and they refuse the same way: a delay at or below 10 milliseconds
// becomes 100. The number is not arbitrary -- 100ms is the delay the oldest
// authoring tools wrote by default, so it is what those files were drawn for.
//
// Chrome, Firefox and Safari all apply it, which is the reason to copy it
// rather than invent one: an app author compares against a browser or against
// React Native on a phone, and the phone is doing this too.
//
// ## Loop counts
//
// Zero means forever, which is the common case and what the NETSCAPE2.0
// extension writes for a looping GIF. A positive count means that many passes
// and then a stop, and the frame it stops on is the last one, not the first:
// the final frame is what the animation was drawn to end on. A single frame, or
// no frames at all, is finished before it starts.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace basalt {

// A delay of 10ms or less means 100ms. See the note above.
inline unsigned clampedImageFrameDelay(unsigned delayMs) {
  return delayMs <= 10U ? 100U : delayMs;
}

struct ImageAnimationStep {
  // Which frame to show, always a valid index when there is at least one.
  std::size_t frame{0};
  // How long until the frame changes. Zero when nothing more is due, which is
  // what a host uses to stop ticking.
  unsigned nextInMs{0};
  // Nothing more will change: one frame, or the last loop is over.
  bool finished{true};
};

// `delaysMs` is one raw delay per frame, as the file carries it; the clamp is
// applied here so no caller has to remember to. `loopCount` is zero for
// forever. `elapsedMs` is how long the image has been animating, which the
// caller accumulates from its own clock rather than reading one here -- see the
// note above.
inline ImageAnimationStep imageAnimationStep(const std::vector<unsigned> &delaysMs,
                                             unsigned loopCount,
                                             std::uint64_t elapsedMs) {
  ImageAnimationStep step;
  if (delaysMs.size() < 2) {
    return step;
  }

  std::uint64_t cycle = 0;
  for (const unsigned delay : delaysMs) {
    cycle += clampedImageFrameDelay(delay);
  }
  if (cycle == 0) {
    return step;
  }

  // The last frame, left showing. A count that has run out is the only way an
  // animation ends, and it ends on what it was drawn to end on.
  if (loopCount > 0 && elapsedMs >= cycle * loopCount) {
    step.frame = delaysMs.size() - 1;
    return step;
  }

  std::uint64_t into = elapsedMs % cycle;
  for (std::size_t index = 0; index < delaysMs.size(); index++) {
    const unsigned delay = clampedImageFrameDelay(delaysMs[index]);
    if (into < delay) {
      step.frame = index;
      step.nextInMs = static_cast<unsigned>(delay - into);
      step.finished = false;
      return step;
    }
    into -= delay;
  }

  // Unreachable: `into` is less than the cycle, and the loop above subtracts
  // the whole cycle. Answering with the last frame rather than asserting,
  // because a wrong frame is better than a dead window.
  step.frame = delaysMs.size() - 1;
  return step;
}

} // namespace basalt
