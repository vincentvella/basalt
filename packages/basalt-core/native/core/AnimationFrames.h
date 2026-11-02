// requestAnimationFrame at the display's rate.
//
// React Native's TimerManager installs requestAnimationFrame as setTimeout(0)
// -- its own comment says so -- and that is fine on a phone, where little calls
// it in a loop. On the desktop it is not: React Native's Animated (JS driver)
// and any app with a playback or render loop re-request a frame from inside the
// callback, and a 0ms timer turns that into a spin as fast as the JavaScript
// thread allows. kino's playback loop held a core and a half doing it, polling
// the audio clock thousands of times a second to change a frame thirty times.
//
// This replaces both globals after TimerManager installs its own (ReactInstance
// calls the bindings hook straight after attachGlobals), and in bridgeless mode
// React Native's JavaScript leaves them alone. Callbacks queue on the JS
// thread; each display frame runs the whole batch with one timestamp, as a
// browser does, and a callback requested during the batch waits for the next
// frame. Frames are asked for only while something is queued, so an idle app
// is not woken at display rate.

#pragma once

#include <jsi/jsi.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <vector>

namespace basalt {

class DisplayChoreographer;

// The queue without the JavaScript: what requestAnimationFrame and
// cancelAnimationFrame do to it, and what a frame takes. Separate so that the
// ordering and the "are frames wanted" answers can be tested without a runtime.
template <typename Callback>
class FrameQueue {
 public:
  // Queues a callback for the next frame; returns its id (never 0).
  int request(Callback callback) {
    const int id = nextId_++;
    pending_.emplace(id, std::move(callback));
    return id;
  }

  // Forgets a queued callback. An id that already ran, or never was, is fine.
  void cancel(int id) {
    pending_.erase(id);
  }

  // This frame's callbacks, in the order they were requested. What they
  // request while running is next frame's.
  std::vector<Callback> take() {
    std::vector<Callback> batch;
    batch.reserve(pending_.size());
    for (auto &entry : pending_) {
      batch.push_back(std::move(entry.second));
    }
    pending_.clear();
    return batch;
  }

  bool empty() const {
    return pending_.empty();
  }

 private:
  int nextId_{1};
  std::map<int, Callback> pending_;
};

class AnimationFrames : public std::enable_shared_from_this<AnimationFrames> {
 public:
  using RunOnJs = std::function<void(std::function<void(facebook::jsi::Runtime &)>)>;

  // Installs display-paced requestAnimationFrame and cancelAnimationFrame into
  // `runtime`, driven by `choreographer`. `runOnJs` runs a task on the JS
  // thread (a host's ReactHost::runOnRuntimeScheduler). JS thread, before the
  // bundle evaluates.
  static void install(facebook::jsi::Runtime &runtime,
                      std::shared_ptr<DisplayChoreographer> choreographer,
                      RunOnJs runOnJs);

  AnimationFrames(std::shared_ptr<DisplayChoreographer> choreographer, RunOnJs runOnJs);

 private:
  int request(facebook::jsi::Function callback);
  void cancel(int id);
  // UI thread, once per display frame while frames are wanted.
  void onDisplayFrame();
  // JS thread.
  void flush(facebook::jsi::Runtime &runtime);
  void updateWanted();

  std::shared_ptr<DisplayChoreographer> choreographer_;
  RunOnJs runOnJs_;
  // JS thread only.
  FrameQueue<facebook::jsi::Function> queue_;
  // At most one flush in flight: a busy JS thread should not find a backlog of
  // frames waiting when it gets free.
  std::atomic<bool> flushQueued_{false};
};

} // namespace basalt
