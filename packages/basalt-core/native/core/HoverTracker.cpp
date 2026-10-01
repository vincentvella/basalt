#include "HoverTracker.h"

namespace basalt {

using facebook::react::ViewEvents;

std::uint16_t hoverListenersFrom(const ViewEvents &events) {
  using Offset = ViewEvents::Offset;
  std::uint16_t mask = HoverListenerNone;
  if (events[Offset::PointerEnter]) {
    mask |= HoverListenerEnter;
  }
  if (events[Offset::PointerLeave]) {
    mask |= HoverListenerLeave;
  }
  if (events[Offset::PointerMove]) {
    mask |= HoverListenerMove;
  }
  if (events[Offset::PointerOver]) {
    mask |= HoverListenerOver;
  }
  if (events[Offset::PointerOut]) {
    mask |= HoverListenerOut;
  }
  if (events[Offset::PointerEnterCapture]) {
    mask |= HoverListenerEnterCapture;
  }
  if (events[Offset::PointerLeaveCapture]) {
    mask |= HoverListenerLeaveCapture;
  }
  if (events[Offset::PointerMoveCapture]) {
    mask |= HoverListenerMoveCapture;
  }
  if (events[Offset::PointerOverCapture]) {
    mask |= HoverListenerOverCapture;
  }
  if (events[Offset::PointerOutCapture]) {
    mask |= HoverListenerOutCapture;
  }
  return mask;
}

bool HoverTracker::admitMove(int tag, std::uint16_t listenersOnPath) {
  if (listenersOnPath != HoverListenerNone) {
    emittedAt_ = tag;
    return true;
  }
  // Nothing here listens. If something did a moment ago, one more move has to
  // go through: it is what tells the processor the cursor left, and it is
  // dispatched at the view the cursor is on now, which is what makes the path
  // diverge from the one it is holding.
  if (emittedAt_ != 0) {
    emittedAt_ = 0;
    return true;
  }
  return false;
}

int HoverTracker::admitLeave() {
  const int tag = emittedAt_;
  emittedAt_ = 0;
  return tag;
}

void HoverTracker::forget() {
  emittedAt_ = 0;
}

} // namespace basalt
