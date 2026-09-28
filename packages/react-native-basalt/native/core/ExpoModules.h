// This platform's Expo modules, and the thing that was missing until now: a way
// to have one at all.
//
// `requireNativeModule('ExpoClipboard')` looks in three places, in order:
// `globalThis.expo.modules[name]`, the legacy bridge proxy, and a TurboModule
// of the same name wrapped by expo-modules-core. The third looks tempting --
// this project already has a TurboModule for the clipboard -- but the wrapper
// copies methods off `Object.getPrototypeOf(turboModule)`, and a C++ TurboModule
// is a jsi host object whose prototype is `Object.prototype`, so what it copies
// is nothing. The first is the real seam, and the one every native platform
// uses.
//
// So an Expo module here is a `expo.NativeModule` instance -- expo's own class,
// installed by ExpoRuntime, which is where `addListener` and the event
// machinery come from -- with host functions set on it. `moduleWithFunctions`
// below is the whole of the framework; everything else is one module's worth of
// behaviour, and each of them is a handful of lines because the behaviour is
// already written. `ExpoClipboard` is core/PlatformServices.h's clipboard, the
// same one React Native's own `Clipboard` module uses.
//
// **What a missing method does is part of the design.** Expo's JavaScript
// checks for each function before calling it -- `if (!ExpoClipboard.getImageAsync)
// throw new UnavailabilityError(...)` -- and reports "not available on this
// platform" naming the method. That is a better answer than a stub returning
// null, so a method this platform cannot implement is left off rather than
// faked.

#pragma once

#include <jsi/jsi.h>

#include <functional>
#include <string>
#include <vector>

namespace basalt {

// Adds this platform's Expo modules to `modules`, the object that becomes
// `globalThis.expo.modules`. Called from installExpoRuntime; there is no reason
// for anything else to call it.
//
// A no-op when the build was not pointed at an expo-modules-core, like
// everything else Expo here.
// The whole of the framework, exported because a capability that ships as its
// own package needs it to build its modules and should not copy it.
//
// `expoModule` is an `expo.NativeModule` instance -- expo's own class, installed
// by ExpoRuntime, which is where `addListener` and the event machinery come
// from. `addExpoFunction` puts a host function on one. Promises come from
// core/JsiPromise.h.
facebook::jsi::Object expoModule(facebook::jsi::Runtime &runtime);

void addExpoFunction(facebook::jsi::Runtime &runtime,
                     facebook::jsi::Object &module,
                     const char *name,
                     unsigned argumentCount,
                     facebook::jsi::HostFunctionType function);

void installExpoModules(facebook::jsi::Runtime &runtime, facebook::jsi::Object &modules);

// Emits an event on an already-installed Expo module, from any thread.
//
// This is the other half of `addExpoFunction`: a function is how JavaScript
// reaches native, and an event is how native reaches JavaScript later. Expo's
// own `NativeModule` is an `EventEmitter`, so `module.addListener('onOutput', f)`
// already works from JavaScript the moment the module is installed -- there was
// simply nothing here that ever emitted.
//
// **Named rather than captured.** The module is looked up as
// `globalThis.expo.modules[moduleName]` at the moment of the emit, rather than
// the caller holding the `jsi::Object` it built at install time. A held object
// outlives its runtime across a Fast Refresh reload and destroying it then
// touches a runtime that is gone; a name survives, and finds the new module
// installed into the new runtime. So a background thread can hold nothing but
// two strings, which is all a reader thread should have to be careful about.
//
// `arguments` builds the event payload and runs on the JavaScript thread, so it
// may use `runtime` freely. Everything it needs from the emitting thread it must
// capture by value.
//
// False when there is nothing to emit into: no host left a runtime runner (see
// JsRuntimeAccess.h), which is the tests and the probe. A module that is not
// installed, or is installed and has no listeners, is not a failure -- an event
// nobody is listening to is the normal case, and Expo's emitEvent does nothing.
bool emitExpoEvent(
    const std::string &moduleName,
    const std::string &eventName,
    std::function<void(facebook::jsi::Runtime &, std::vector<facebook::jsi::Value> &)> arguments);

// Adds `expo.getViewConfig`, which is how expo-modules-core turns an Expo view
// into a React Native component. Separate from the modules because it goes on
// `expo` itself rather than on `expo.modules`.
void installExpoViewConfigs(facebook::jsi::Runtime &runtime, facebook::jsi::Object &expo);

// The app's Expo config -- `app.json` or `app.config.js`, resolved -- which is
// what `Constants.expoConfig` is and what expo-linking reads to find the app's
// URI scheme. Without it `Constants.expoConfig` is null and an app that reads
// `Constants.expoConfig.extra.something` crashes on the property access.
//
// iOS and Android embed it in the app bundle at build time, from a script Expo
// runs during the native build. This platform's equivalent moment is bundling:
// the bundler writes `app.config.json` beside the bundle, which is why this
// takes the bundle's path rather than the config's. Nothing being there is not
// an error -- a plain React Native app has no Expo config and never asks for
// one.
//
// Call before the runtime is installed; the hosts do it as they start.
void loadExpoAppConfigBeside(const std::string &bundlePath);

// The app's name from that config -- `name` in app.json -- or empty when there
// is no config or it names nothing. What a host titles its window with, rather
// than with its own name.
std::string expoAppName();

} // namespace basalt
