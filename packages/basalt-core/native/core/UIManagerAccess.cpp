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
