// The listener half of core/DragAndDrop.h.
//
// Here rather than in the header because the header is included by the view
// libraries, which link no core -- so anything with state has to live in the
// half the hosts link. See the CMakeLists comment about FocusRing.h and
// Backface.h for the same rule stated where it bites.

#include "DragAndDrop.h"

#include <mutex>

namespace basalt {
namespace {

std::mutex &lock() {
  static std::mutex value;
  return value;
}

std::function<void(const DropEvent &)> &listener() {
  static std::function<void(const DropEvent &)> value;
  return value;
}

} // namespace

void setDropListener(std::function<void(const DropEvent &)> value) {
  const std::lock_guard<std::mutex> guard(lock());
  listener() = std::move(value);
}

void reportDrop(const DropEvent &event) {
  std::function<void(const DropEvent &)> toCall;
  {
    const std::lock_guard<std::mutex> guard(lock());
    toCall = listener();
  }
  // Outside the lock: it emits a device event, and holding a lock across a
  // call into the runtime is how a deadlock is built.
  if (toCall) {
    toCall(event);
  }
}

} // namespace basalt
