#include "UIManagerAccess.h"

#include <glog/logging.h>

#include <mutex>

namespace basalt {

namespace {

std::weak_ptr<facebook::react::UIManager> &held() {
  static std::weak_ptr<facebook::react::UIManager> uiManager;
  return uiManager;
}

} // namespace

void setSharedUIManager(std::weak_ptr<facebook::react::UIManager> uiManager) {
  held() = std::move(uiManager);
}

std::shared_ptr<facebook::react::UIManager> sharedUIManager() {
  return held().lock();
}

bool reportMountedSurface(facebook::react::SurfaceId surfaceId) {
  const auto uiManager = sharedUIManager();
  if (!uiManager) {
    return false;
  }
  // `visit` answers false without running the callback when the surface is not
  // there, and says it is safe from any thread. See the header for why this
  // question is the one that matters.
  const bool registered = uiManager->getShadowTreeRegistry().visit(
      surfaceId, [](const facebook::react::ShadowTree &) {});
  if (!registered) {
    // Every time, because this should be rare: it means a transaction was
    // applied after its surface had gone, which is the race this guards. A run
    // that logs this in quantity is saying the refusal is not rare, and a
    // *healthy* run that logs it at all is saying the question is wrong.
    LOG(WARNING) << "mount report refused: surface " << surfaceId
                 << " is no longer registered";
    return false;
  }
  // Once per process, so that a suite can tell "the guard is letting mounts
  // through" from "the guard refuses everything". Refusing everything is the
  // failure this has no other way of noticing: nothing asserts that a mount
  // hook ran, so an over-eager refusal would stop Reanimated silently and leave
  // every test green. See docs/backlog/testing.md.
  static std::once_flag reported;
  std::call_once(reported, [surfaceId] {
    LOG(INFO) << "mount reported to the UIManager, surface " << surfaceId;
  });
  uiManager->reportMount(surfaceId);
  return true;
}

namespace {

EventListenerInstaller &installer() {
  static EventListenerInstaller function;
  return function;
}

} // namespace

void setEventListenerInstaller(EventListenerInstaller function) {
  installer() = std::move(function);
}

bool installEventListener(std::shared_ptr<const facebook::react::EventListener> listener) {
  if (!installer()) {
    return false;
  }
  installer()(std::move(listener));
  return true;
}

} // namespace basalt
