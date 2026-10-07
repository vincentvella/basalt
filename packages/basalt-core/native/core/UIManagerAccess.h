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

// `reportMount`, but only for a surface the UIManager still has, which is a
// workaround for an upstream use-after-free and not a tidying-up.
//
// `UIManager::mountHooks_` holds raw, non-owning pointers and unregistering is
// the owner's job. `Scheduler` registers one and `~Scheduler` does not take it
// out again, so from the moment `destroyReactInstance` writes
// `scheduler_ = nullptr` the list contains a freed `EventPerformanceLogger` --
// three statements before it tells a mounting manager anything, so the epoch
// guard in MountingWalk.h cannot help. A mount draining on the main queue in
// that gap used to walk the list and call a virtual method on freed memory.
// See docs/backlog/testing.md.
//
// What makes a probe enough: `stopAllSurfaces()` runs *before* the Scheduler is
// freed and takes the shadow trees out of the registry, so "the hook is
// dangling" always implies "the surface has gone". Asking whether the surface is
// still registered therefore refuses exactly the dangerous case, and the healthy
// case is unaffected because a live surface is still in there. It also skips a
// call that had nothing to report: with no root shadow node `reportMount` only
// tells every hook the tree unmounted.
//
// Not airtight, and it is worth being clear about that. The surface could in
// principle be stopped between the question and the call, which is a few
// instructions rather than three statements of teardown. The fix is one line
// upstream, in `~Scheduler`.
//
// Returns whether it reported.
bool reportMountedSurface(facebook::react::SurfaceId surfaceId);

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
