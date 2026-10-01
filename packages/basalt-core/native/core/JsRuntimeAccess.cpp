#include "JsRuntimeAccess.h"

#include <mutex>
#include <stdexcept>
#include <utility>

namespace basalt {

namespace {

// A mutex rather than a bare static, because unlike the UIManager this is read
// from whatever thread a subprocess reader happens to be on while the main
// thread may be clearing it on the way to quitting.
std::mutex &lock() {
  static std::mutex mutex;
  return mutex;
}

RuntimeRunner &runner() {
  static RuntimeRunner function;
  return function;
}

class RunnerCallInvoker : public facebook::react::CallInvoker {
 public:
  void invokeAsync(facebook::react::CallFunc &&func) noexcept override {
    runOnJsRuntime(std::move(func));
  }

  void invokeSync(facebook::react::CallFunc && /*func*/) override {
    // See the header: the only door a host leaves open posts. A caller wanting
    // this wants something this platform cannot give it, and saying so here is
    // better than a deadlock in whatever it was doing.
    throw std::logic_error(
        "basalt: invokeSync on the runtime-runner CallInvoker. This invoker is "
        "ReactHost::runOnRuntimeScheduler, which posts and cannot run work inline; "
        "see core/JsRuntimeAccess.h.");
  }
};

} // namespace

void setRuntimeRunner(RuntimeRunner function) {
  std::lock_guard<std::mutex> guard(lock());
  runner() = std::move(function);
}

bool runOnJsRuntime(std::function<void(facebook::jsi::Runtime &)> work) {
  // Copied out under the lock and called outside it: the runner posts to another
  // thread, and holding a lock across a call into the host is how the drop and
  // key listeners were careful too.
  RuntimeRunner function;
  {
    std::lock_guard<std::mutex> guard(lock());
    function = runner();
  }
  if (!function) {
    return false;
  }
  function(std::move(work));
  return true;
}

std::shared_ptr<facebook::react::CallInvoker> jsCallInvoker() {
  static std::shared_ptr<facebook::react::CallInvoker> invoker =
      std::make_shared<RunnerCallInvoker>();
  return invoker;
}

} // namespace basalt
