// Reaching the JavaScript thread from native code that is not a TurboModule.
//
// A TurboModule is handed a CallInvoker when it is constructed, which is how
// every module here answers asynchronously: the drop listener, the key listener
// and the file dialogs all capture `jsInvoker_` and call back through it. An
// Expo module is not a TurboModule -- it is a plain jsi object installed into
// `globalThis.expo.modules`, built by a function that is given a runtime and
// nothing else (see ExpoModules.h) -- so it has no invoker to capture and no way
// to emit an event or settle a promise later.
//
// Until now nothing needed one, because every Expo module here was synchronous
// underneath and JsiPromise.h's already-settled promises were the honest answer.
// The first module with a subprocess in it breaks that: output arrives on a
// reader thread minutes after the call that started it returned.
//
// What the hosts have is `ReactHost::runOnRuntimeScheduler`, which is the same
// door the reload path and the dev menu already go through. So each host leaves
// it here as it starts, the way it leaves the event-listener installer in
// UIManagerAccess.h, and core code that needs the JavaScript thread asks for it
// by name rather than by owning a pointer to the host.
//
// **Asynchronous only.** `runOnRuntimeScheduler` posts; it cannot run work
// inline and wait. `jsCallInvoker()` below is a CallInvoker over it, which makes
// React Native's own AsyncPromise available to an Expo module, and its
// `invokeSync` throws rather than pretending: a synchronous call from another
// thread into a runtime that is busy is a deadlock, and the invoker that offers
// it would be lying about what this door can do.

#pragma once

#include <ReactCommon/CallInvoker.h>

#include <jsi/jsi.h>

#include <functional>
#include <memory>

namespace basalt {

// Posts `work` to the JavaScript thread. Anything that touches a runtime from
// another thread goes through here.
using RuntimeRunner = std::function<void(std::function<void(facebook::jsi::Runtime &)>)>;

// Called by each host once its ReactHost exists, and again with nullptr as it
// shuts down -- work posted to a host that is going away must not run.
void setRuntimeRunner(RuntimeRunner runner);

// False when no host left one, which is every build that is not a host: the
// tests and the portability probe. The caller's `work` is then never run, and a
// caller that has something to report is the one that decides whether silence is
// acceptable.
bool runOnJsRuntime(std::function<void(facebook::jsi::Runtime &)> work);

// The above as a CallInvoker, for React Native's bridging helpers -- chiefly
// `AsyncPromise`, which is how a native call that finishes later hands back a
// promise. One instance, shared; it holds nothing but the call to
// runOnJsRuntime, so it stays valid across a reload that replaces the runner.
std::shared_ptr<facebook::react::CallInvoker> jsCallInvoker();

} // namespace basalt
