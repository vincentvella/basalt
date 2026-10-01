// The UIManager, for the one thing that needs it outside the renderer.
//
// Reanimated commits to the shadow tree itself -- that is how an animated style
// reaches the screen without a React re-render -- so it needs the UIManager,
// and it is constructed from JavaScript, where no host object is in reach.
//
// ReactCxxPlatform already hands the UIManager to the mounting manager through
// `IMountingManager::setUIManager`, whose default implementation ignores it. So
// each platform's mounting manager overrides that and puts it here, and the
// module takes it back out. Weak, because the UIManager belongs to the
// scheduler and outlives neither a reload nor a quit.

#pragma once

#include <react/renderer/core/EventListener.h>
#include <react/renderer/uimanager/UIManager.h>

#include <functional>

#include <memory>

namespace basalt {

void setSharedUIManager(std::weak_ptr<facebook::react::UIManager> uiManager);
std::shared_ptr<facebook::react::UIManager> sharedUIManager();

// Watching every event Fabric dispatches.
//
// `Scheduler::addEventListener` is how a library sees events before the
// components do -- Reanimated needs it so that a scroll handler or a gesture
// callback declared as a worklet runs on the UI thread rather than a frame
// later on the JavaScript one. The scheduler belongs to ReactHost, which only
// the host has, so the host leaves a way to reach it here.
using EventListenerInstaller =
    std::function<void(std::shared_ptr<const facebook::react::EventListener>)>;

void setEventListenerInstaller(EventListenerInstaller installer);

// False when no host left one, which is every build that is not a host --
// the tests and the portability probe.
bool installEventListener(std::shared_ptr<const facebook::react::EventListener> listener);

} // namespace basalt
