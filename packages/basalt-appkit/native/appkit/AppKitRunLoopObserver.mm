#import "AppKitRunLoopObserver.h"

#include <react/utils/RunLoopObserverManager.h>

namespace basalt {

CFRunLoopObserverRef installRunLoopObserver(
    std::shared_ptr<facebook::react::RunLoopObserverManager> manager) {
  // The block captures the shared_ptr, so the manager outlives any beat already
  // in flight. CFRunLoopObserverCreateWithHandler copies the block onto the
  // heap and releases it when the observer is released.
  CFRunLoopObserverRef observer = CFRunLoopObserverCreateWithHandler(
      kCFAllocatorDefault,
      kCFRunLoopBeforeWaiting,
      /*repeats=*/true,
      // Order: after AppKit's own observers, so this runs having let the frame
      // settle rather than ahead of it. The GTK side makes the same choice with
      // G_PRIORITY_DEFAULT_IDLE, for the same reason.
      /*order=*/CFIndex{2000000},
      ^(CFRunLoopObserverRef /*self*/, CFRunLoopActivity /*activity*/) {
        if (manager != nullptr) {
          manager->onRender();
        }
      });

  // Both modes. The default mode is where AppKit spends its time idle; a modal
  // panel or a live window resize switches to another, and a beat that stopped
  // there would stop delivering every event JavaScript is waiting on until the
  // user let go of the mouse.
  CFRunLoopAddObserver(CFRunLoopGetMain(), observer, kCFRunLoopCommonModes);
  return observer;
}

void removeRunLoopObserver(CFRunLoopObserverRef observer) {
  if (observer == nullptr) {
    return;
  }
  CFRunLoopRemoveObserver(CFRunLoopGetMain(), observer, kCFRunLoopCommonModes);
  CFRelease(observer);
}

} // namespace basalt
