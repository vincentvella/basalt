// The pull half of <RefreshControl>.
//
// A desktop scroll view has no rubber band. There is nothing to drag past the
// top, no elastic to stretch, and no released-at-a-distance moment to fire on
// -- which is why React Native's own pull-to-refresh exists on phones and not
// here. What a desktop does have is a wheel that keeps reporting upward scroll
// after the offset has already reached zero, and that is the same gesture in
// every way that matters: the person is asking for something above the top of
// the list.
//
// So this counts that. The scroll view clamps its offset as it always has --
// nothing here moves anything -- and hands over how far past the top the
// gesture asked to go. Once that passes the threshold, `onRefresh` fires once,
// and not again until the gesture stops.
//
// Portable for the usual reason: the counting is identical on three desktops
// and the wheel plumbing is not. It also makes the one part that is easy to get
// wrong -- firing repeatedly while somebody keeps scrolling -- testable without
// a mouse.

#pragma once

#include "DesktopControls.h"

#include <react/renderer/core/ReactPrimitives.h>

#include <unordered_map>

namespace basalt {

class PullToRefreshTracker {
 public:
  // Says that `controlTag` is the <RefreshControl> belonging to `scrollTag`.
  // Called by the mounting manager when a PullToRefreshView is parented, which
  // is the only place the relationship is visible.
  void attach(facebook::react::Tag scrollTag, facebook::react::Tag controlTag);

  // Drops whatever `tag` was, whether it was a scroll view or a control.
  void forget(facebook::react::Tag tag);

  // The refresh control for a scroll view, or 0 for a scroll view with none.
  facebook::react::Tag controlFor(facebook::react::Tag scrollTag) const;

  // How far past the top this gesture has asked to go, accumulated. `amount`
  // is in points and positive; a downward scroll or any scroll that actually
  // moved the view calls `release` instead.
  //
  // Returns true exactly once per gesture: on the step that crosses the
  // threshold. A scroll view with no refresh control always returns false, so
  // the caller needs no separate check.
  bool pull(facebook::react::Tag scrollTag, double amount);

  // The gesture ended, or moved the scroll view off the top. Re-arms the pull.
  void release(facebook::react::Tag scrollTag);

 private:
  struct Entry {
    facebook::react::Tag control{0};
    double pulled{0.0};
    // Whether this gesture has already fired. Without it a wheel held down
    // fires `onRefresh` sixty times a second, and React Native's contract is
    // one call per pull.
    bool fired{false};
  };

  std::unordered_map<facebook::react::Tag, Entry> byScroll_;
  // The other direction, so `forget` works when a control is deleted and its
  // scroll view is not -- which is what unmounting a <RefreshControl> looks
  // like.
  std::unordered_map<facebook::react::Tag, facebook::react::Tag> scrollByControl_;
};

} // namespace basalt
