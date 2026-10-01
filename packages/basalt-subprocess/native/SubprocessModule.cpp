#include "SubprocessModule.h"

#include "Subprocess.h"

#include "JsiPromise.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

namespace {

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;
using facebook::react::TurboModule;

std::string stringProperty(Runtime &runtime, const Object &options, const char *name) {
  const Value value = options.getProperty(runtime, name);
  return value.isString() ? value.asString(runtime).utf8(runtime) : std::string{};
}

// `env: {NAME: 'value'}`, which is the shape every JavaScript API for this uses.
EnvironmentOverrides environmentFrom(Runtime &runtime, const Object &options) {
  EnvironmentOverrides overrides;
  const Value value = options.getProperty(runtime, "env");
  if (!value.isObject()) {
    return overrides;
  }
  const Object env = value.asObject(runtime);
  const auto names = env.getPropertyNames(runtime);
  const size_t count = names.size(runtime);
  for (size_t i = 0; i < count; i++) {
    const String name = names.getValueAtIndex(runtime, i).asString(runtime);
    const Value entry = env.getProperty(runtime, name);
    // A variable whose value is not a string is a mistake worth ignoring rather
    // than stringifying: `undefined` would become the four letters of it.
    if (entry.isString()) {
      overrides.emplace_back(name.utf8(runtime), entry.asString(runtime).utf8(runtime));
    }
  }
  return overrides;
}

} // namespace

namespace {

// Every live module, newest last.
//
// ReactCxxPlatform builds a *new* module for each lookup rather than caching
// one -- an app's start-up can make half a dozen, and they come and go while it
// runs. So "the module that installed the listeners" is not a thing, and a
// destructor that clears them takes them away from the ones still serving.
//
// This was first written as an ownership guard, on the assumption that one
// module is live at a time. That assumption is wrong, and it cost an evening in
// core/WindowsModule.cpp, where the same shape left every keyboard shortcut in
// an app silently dead. The listeners belong to the set: installed when the
// first module appears, handed on when one goes, cleared only when the last
// does.
std::mutex &liveMutex() {
  static std::mutex mutex;
  return mutex;
}

std::vector<SubprocessModule *> &liveModules() {
  static std::vector<SubprocessModule *> modules;
  return modules;
}

} // namespace

SubprocessModule::SubprocessModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["spawn"] = MethodMetadata{1, spawn};
  methodMap_["kill"] = MethodMetadata{1, kill};
  methodMap_["isRunning"] = MethodMetadata{1, isRunning};
}

void SubprocessModule::attach() {
  {
    const std::lock_guard<std::mutex> lock(liveMutex());
    liveModules().push_back(this);
  }

  // Output and exit arrive on whatever thread is watching the child, and
  // emitDeviceEvent marshals to the JavaScript one -- the same path the window
  // and key listeners in core take.
  //
  // **A weak reference, not `this`.** Those two listeners are the only ones in
  // this project that can fire from a thread the module knows nothing about: a
  // child writes when it writes. Every other listener -- keys, drops, window
  // state -- is called on the thread that also destroys the module, so a bare
  // `this` is safe there and is not here. `lock()` either fails, because the
  // module has gone, or yields a reference that keeps it alive for the call.
  const std::weak_ptr<SubprocessModule> weak = weak_from_this();

  setSubprocessListeners(
      [weak](int pid, const std::string &data, bool isStandardError) {
        const std::shared_ptr<SubprocessModule> self = weak.lock();
        if (!self) {
          return;
        }
        self->emitDeviceEvent(kOutputEvent, [pid, data, isStandardError](
                                          Runtime &runtime, std::vector<Value> &args) {
          Object payload(runtime);
          payload.setProperty(runtime, "pid", Value(pid));
          payload.setProperty(runtime, "data", String::createFromUtf8(runtime, data));
          payload.setProperty(
              runtime, "stream",
              String::createFromAscii(runtime, isStandardError ? "stderr" : "stdout"));
          args.emplace_back(runtime, payload);
        });
      },
      [weak](int pid, int code) {
        const std::shared_ptr<SubprocessModule> self = weak.lock();
        if (!self) {
          return;
        }
        self->emitDeviceEvent(kExitEvent, [pid, code](Runtime &runtime, std::vector<Value> &args) {
          Object payload(runtime);
          payload.setProperty(runtime, "pid", Value(pid));
          payload.setProperty(runtime, "code", Value(code));
          args.emplace_back(runtime, payload);
        });
      });
}

SubprocessModule::~SubprocessModule() {
  SubprocessModule *successor = nullptr;
  {
    const std::lock_guard<std::mutex> lock(liveMutex());
    std::vector<SubprocessModule *> &live = liveModules();
    live.erase(std::remove(live.begin(), live.end(), this), live.end());
    if (!live.empty()) {
      successor = live.back();
    }
  }
  if (successor != nullptr) {
    // Handed over, not cleared; see liveModules(). A child is still running and
    // still has output to report, and the module it reports through must be one
    // that is still here.
    successor->attach();
    return;
  }
  setSubprocessListeners(nullptr, nullptr);
}

Value SubprocessModule::spawn(Runtime &runtime,
                              TurboModule & /*module*/,
                              const Value *args,
                              size_t count) {
  if (count == 0 || !args[0].isObject()) {
    return rejected(runtime, "BasaltSubprocess.spawn needs an options object");
  }
  const Object options = args[0].asObject(runtime);

  SpawnRequest request;
  request.command = stringProperty(runtime, options, "command");
  request.cwd = stringProperty(runtime, options, "cwd");
  request.env = environmentFrom(runtime, options);
  if (request.command.empty()) {
    return rejected(runtime, "BasaltSubprocess.spawn needs a command");
  }

  std::string error;
  const int pid = spawnSubprocess(request, error);
  if (pid == 0) {
    return rejected(runtime, error);
  }
  // Settled, because starting a process is not slow. The signature is async
  // because on a platform where it crosses a boundary it would be; see
  // core/JsiPromise.h.
  return resolved(runtime, Value(pid));
}

Value SubprocessModule::kill(Runtime & /*runtime*/,
                             TurboModule & /*module*/,
                             const Value *args,
                             size_t count) {
  if (count == 0 || !args[0].isNumber()) {
    return Value(false);
  }
  return Value(killSubprocess(static_cast<int>(args[0].asNumber())));
}

Value SubprocessModule::isRunning(Runtime & /*runtime*/,
                                  TurboModule & /*module*/,
                                  const Value *args,
                                  size_t count) {
  if (count == 0 || !args[0].isNumber()) {
    return Value(false);
  }
  return Value(isSubprocessRunning(static_cast<int>(args[0].asNumber())));
}

std::shared_ptr<facebook::react::TurboModule> makeSubprocessTurboModule(
    const std::string &name,
    const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker) {
  if (name != SubprocessModule::kModuleName) {
    return nullptr;
  }
  // Only where there is something underneath. The probe links no platform, and
  // a module whose every call failed would be worse than one that is absent:
  // `TurboModuleRegistry.get` answering null is a question JavaScript can ask.
  if (!subprocessSupported()) {
    return nullptr;
  }
  // Built, then subscribed: `attach` needs a shared_ptr to already own it.
  const std::shared_ptr<SubprocessModule> module = std::make_shared<SubprocessModule>(jsInvoker);
  module->attach();
  return module;
}

} // namespace basalt
