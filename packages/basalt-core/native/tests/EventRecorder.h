// A place for a unit test to watch events arrive.
//
// Neither host suite could see an event. The managers ask
// `eventEmitterForTag` for an emitter and call `onChange`, `onFocus`,
// `onKeyPress` on it, and the shadow views the tests hand-build carry no
// emitter at all -- so `test_appkit_textinput.mm` says, accurately, that what
// it pins is "that the round trip did not throw". Nothing below the
// end-to-end suite knew whether an event fired, let alone in what order.
//
// ## Why there is no stub emitter here
//
// The obvious fix is a fake emitter handed back through the lookup, and it
// cannot work: `TextInputEventEmitter` contains the word `virtual` zero
// times, so there is nothing to override. Every `onX` calls the non-virtual
// `EventEmitter::dispatchEvent`, which locks a `weak_ptr<const
// EventDispatcher>` and returns quietly when it is empty. That empty weak
// pointer is exactly the state the suites were in.
//
// ## So this builds the real thing instead
//
// `EventDispatcher` offers a listener -- `std::function<bool(const RawEvent
// &)>` -- and consults it at the top of `dispatchEvent`, before the logger and
// before the queue. Returning true says the event was handled and stops the
// default dispatch, so nothing downstream ever runs and no beat is needed to
// flush anything.
//
// The one awkward piece is that `EventQueue`'s constructor calls
// `setBeatCallback` on the beat immediately, so it cannot be null, and
// `EventBeat` holds a `RuntimeScheduler &`. That sounds like the whole JS
// machinery and is not: `RuntimeScheduler` takes a `RuntimeExecutor`, which is
// a `std::function`, and a runtime only appears when something invokes it.
// Nothing here does.
//
// ## What can be asserted, and what cannot
//
// Event *types* and their *order*, which is what the payload-free half of an
// event is. Payload values cannot: `TextInputEventEmitter` builds a
// `jsi::Object` through a `ValueFactory`, and reading one back needs a
// `jsi::Runtime` -- which is the machinery these suites exist to avoid. What
// a value ends up as is the end-to-end suite's to check; that the event fired
// at all, and before or after its neighbour, is this one's.

#pragma once

#include <react/renderer/core/EventDispatcher.h>
#include <react/renderer/core/EventEmitter.h>
#include <react/renderer/core/EventListener.h>
#include <react/renderer/core/EventQueueProcessor.h>
#include <react/renderer/core/RawEvent.h>
#include <react/renderer/runtimescheduler/RuntimeScheduler.h>

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace basalt::testing {

// An EventBeat that never beats. `EventQueue` requires one and this test never
// reaches the queue, so what matters is that it exists and does nothing.
class IdleEventBeat : public facebook::react::EventBeat {
 public:
  IdleEventBeat(std::shared_ptr<OwnerBox> ownerBox,
                facebook::react::RuntimeScheduler &scheduler)
      : EventBeat(std::move(ownerBox), scheduler) {}

  void request() const override {}
};

class EventRecorder {
 public:
  EventRecorder()
      : scheduler_([](std::function<void(facebook::jsi::Runtime &)> &&) {}),
        dispatcher_(std::make_shared<const facebook::react::EventDispatcher>(
            facebook::react::EventQueueProcessor(nullptr, nullptr, nullptr, {}),
            std::make_unique<IdleEventBeat>(
                std::make_shared<facebook::react::EventBeat::OwnerBox>(), scheduler_),
            [](const facebook::react::StateUpdate &) {},
            std::weak_ptr<facebook::react::EventLogger>{})),
        listener_(std::make_shared<const facebook::react::EventListener>(
            [this](const facebook::react::RawEvent &event) {
              std::scoped_lock lock(mutex_);
              seen_.push_back(event.type);
              // Handled: the event stops here rather than going on to a queue
              // that would need a JavaScript thread to drain it.
              return true;
            })) {
    dispatcher_->addListener(listener_);
  }

  // An emitter of the kind a manager will look for. The event target is null,
  // which `EventEmitter::dispatchEvent` moves into the `RawEvent` without
  // touching -- a tag is not what is being asserted here.
  // Non-const, because that is what `ShadowView::eventEmitter` is at this
  // React Native. Every `onX` is a const method, so it makes no difference to
  // the caller.
  template <typename Emitter>
  std::shared_ptr<Emitter> emitter() const {
    return std::make_shared<Emitter>(nullptr, dispatcher_);
  }

  // The event types seen so far, in the order they were dispatched. Names are
  // React Native's own normalised spelling: see `normalizeEventType`.
  std::vector<std::string> seen() const {
    std::scoped_lock lock(mutex_);
    return seen_;
  }

  void clear() {
    std::scoped_lock lock(mutex_);
    seen_.clear();
  }

 private:
  facebook::react::RuntimeScheduler scheduler_;
  std::shared_ptr<const facebook::react::EventDispatcher> dispatcher_;
  std::shared_ptr<const facebook::react::EventListener> listener_;
  mutable std::mutex mutex_;
  std::vector<std::string> seen_;
};

} // namespace basalt::testing
