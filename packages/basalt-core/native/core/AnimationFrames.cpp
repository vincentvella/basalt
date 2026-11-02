#include "AnimationFrames.h"

#include "DisplayChoreographer.h"

#include <exception>
#include <utility>

namespace basalt {

namespace jsi = facebook::jsi;

AnimationFrames::AnimationFrames(std::shared_ptr<DisplayChoreographer> choreographer, RunOnJs runOnJs)
    : choreographer_(std::move(choreographer)), runOnJs_(std::move(runOnJs)) {}

void AnimationFrames::install(jsi::Runtime &runtime,
                              std::shared_ptr<DisplayChoreographer> choreographer,
                              RunOnJs runOnJs) {
  if (choreographer == nullptr) {
    return;
  }
  auto frames = std::make_shared<AnimationFrames>(choreographer, std::move(runOnJs));

  // The tick reaches this runtime's queue only while it lives: a reload makes
  // a new runtime and installs a new listener over this one.
  std::weak_ptr<AnimationFrames> weak = frames;
  choreographer->setFrameListener([weak] {
    if (auto self = weak.lock()) {
      self->onDisplayFrame();
    }
  });

  auto global = runtime.global();
  global.setProperty(
      runtime,
      "requestAnimationFrame",
      jsi::Function::createFromHostFunction(
          runtime,
          jsi::PropNameID::forAscii(runtime, "requestAnimationFrame"),
          1,
          [frames](jsi::Runtime &rt, const jsi::Value &, const jsi::Value *args, size_t count) {
            if (count < 1 || !args[0].isObject() || !args[0].getObject(rt).isFunction(rt)) {
              throw jsi::JSError(rt, "requestAnimationFrame needs a function");
            }
            return jsi::Value(frames->request(args[0].getObject(rt).getFunction(rt)));
          }));
  global.setProperty(
      runtime,
      "cancelAnimationFrame",
      jsi::Function::createFromHostFunction(
          runtime,
          jsi::PropNameID::forAscii(runtime, "cancelAnimationFrame"),
          1,
          [frames](jsi::Runtime &, const jsi::Value &, const jsi::Value *args, size_t count) {
            if (count >= 1 && args[0].isNumber()) {
              frames->cancel(static_cast<int>(args[0].asNumber()));
            }
            return jsi::Value::undefined();
          }));
}

int AnimationFrames::request(jsi::Function callback) {
  const int id = queue_.request(std::move(callback));
  updateWanted();
  return id;
}

void AnimationFrames::cancel(int id) {
  queue_.cancel(id);
  updateWanted();
}

void AnimationFrames::updateWanted() {
  choreographer_->setFramesWanted(!queue_.empty());
}

void AnimationFrames::onDisplayFrame() {
  if (flushQueued_.exchange(true)) {
    return;
  }
  std::weak_ptr<AnimationFrames> weak = shared_from_this();
  runOnJs_([weak](jsi::Runtime &runtime) {
    if (auto self = weak.lock()) {
      self->flush(runtime);
    }
  });
}

void AnimationFrames::flush(jsi::Runtime &runtime) {
  flushQueued_ = false;
  auto batch = queue_.take();
  // Before running them: whatever they request is next frame's, and asks again.
  updateWanted();
  if (batch.empty()) {
    return;
  }

  // One timestamp for the whole frame, on performance.now()'s clock, as a
  // browser passes.
  auto performance = runtime.global().getPropertyAsObject(runtime, "performance");
  const jsi::Value now =
      performance.getPropertyAsFunction(runtime, "now").callWithThis(runtime, performance);

  // A callback that throws does not cost the rest their frame; the first error
  // is rethrown afterwards so it still reaches the error handler.
  std::exception_ptr first;
  for (auto &callback : batch) {
    try {
      callback.call(runtime, now);
    } catch (...) {
      if (!first) {
        first = std::current_exception();
      }
    }
  }
  if (first) {
    std::rethrow_exception(first);
  }
}

} // namespace basalt
