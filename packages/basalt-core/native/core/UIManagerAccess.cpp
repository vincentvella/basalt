#include "UIManagerAccess.h"

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
    return false;
  }
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
