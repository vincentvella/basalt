#include "WindowsModule.h"

#include "KeyEvents.h"

#include "PlatformServices.h"
#include "DragAndDrop.h"
#include "WindowControl.h"
#include "WindowHost.h"

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

#include <algorithm>
#include <string>
#include <vector>
#include <utility>

namespace basalt {

namespace {

using facebook::jsi::Array;
using facebook::jsi::Object;
using facebook::react::Tag;
using facebook::jsi::Runtime;
using facebook::jsi::String;
using facebook::jsi::Value;
using facebook::react::TurboModule;

std::string stringProperty(Runtime &runtime, const Object &options, const char *name) {
  const Value value = options.getProperty(runtime, name);
  return value.isString() ? value.asString(runtime).utf8(runtime) : std::string{};
}

double numberProperty(Runtime &runtime, const Object &options, const char *name, double fallback) {
  const Value value = options.getProperty(runtime, name);
  return value.isNumber() ? value.asNumber() : fallback;
}

} // namespace

namespace {

// Every live module, newest last.
//
// ReactCxxPlatform builds a *new* module for each lookup rather than caching
// one: kino's start-up makes six, and they come and go while the app runs. So
// "the module that installed the listener" is not a thing -- the one that
// installed it is routinely destroyed while others are still serving, and an
// unconditional clear in its destructor took the listeners with it. Keys were
// claimed, presses were matched, and `reportKey` then found no listener and
// dropped the answer. Nothing failed and nothing was logged; every declared
// shortcut in the app was simply dead.
//
// So the listeners belong to the *set*, not to an instance: installed when the
// first module appears, dispatched to whichever is live, and cleared only when
// the last one goes.
std::mutex &liveMutex() {
  static std::mutex mutex;
  return mutex;
}

std::vector<DesktopWindowsModule *> &liveModules() {
  static std::vector<DesktopWindowsModule *> modules;
  return modules;
}

} // namespace

DesktopWindowsModule::DesktopWindowsModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["open"] = MethodMetadata{1, open};
  methodMap_["close"] = MethodMetadata{1, close};
  methodMap_["getWindows"] = MethodMetadata{0, getWindows};
  methodMap_["interceptClose"] = MethodMetadata{2, interceptClose};
  methodMap_["interceptQuit"] = MethodMetadata{1, interceptQuit};
  methodMap_["quit"] = MethodMetadata{0, quit};
  methodMap_["getDisplays"] = MethodMetadata{0, getDisplays};
  methodMap_["getPointerPosition"] = MethodMetadata{0, getPointerPosition};
  // Which keys a view handles. Two arguments rather than a props diff, because a
  // <View> has nowhere in its C++ props for a list of combinations and a
  // callback; see src/KeyHandler.tsx for why that rules out the nativeID idiom
  // <DropTarget> uses.
  methodMap_["setHandledKeys"] = MethodMetadata{2, setHandledKeys};
  methodMap_["clearHandledKeys"] = MethodMetadata{1, clearHandledKeys};
  // What a NativeEventEmitter over this module calls; the event goes out as a
  // device event either way.
  methodMap_["addListener"] = MethodMetadata{1, noop};
  methodMap_["removeListeners"] = MethodMetadata{1, noop};

  {
    const std::lock_guard<std::mutex> lock(liveMutex());
    liveModules().push_back(this);
  }
  installListeners();
}

/**
 * Points every seam at this module.
 *
 * Called from the constructor, and again from another module's destructor when
 * that one was the one the seams pointed at -- see liveModules(). Each of these
 * captures `this`, which is safe only for as long as this module is live, and
 * keeping that true is the whole job of the live set.
 */
void DesktopWindowsModule::installListeners() {
  // A window closed by the person rather than by the app. Without this the
  // `<Window>` that opened it would go on believing it is open; see
  // core/WindowHost.h.
  setHostWindowClosedListener([this](facebook::react::SurfaceId surfaceId) {
    emitDeviceEvent(kWindowClosedEvent,
                    [surfaceId](Runtime & /*runtime*/, std::vector<Value> &args) {
                      args.emplace_back(Value(static_cast<int>(surfaceId)));
                    });
  });

  // Somebody tried to close a window that asked to be asked first. The window
  // is still open; what happens next is the app's decision.
  setHostWindowCloseRequestListener([this](facebook::react::SurfaceId surfaceId) {
    emitDeviceEvent(kWindowCloseRequestedEvent,
                    [surfaceId](Runtime & /*runtime*/, std::vector<Value> &args) {
                      args.emplace_back(Value(static_cast<int>(surfaceId)));
                    });
  });

  // And somebody trying to quit an application that asked to be asked. No
  // argument: there is only one application.
  // A monitor plugged in, unplugged or rearranged. No payload: an app that
  // cares re-reads the list, which is the only way to be right when several
  // changes arrive together.
  setDisplaysListener([this]() {
    emitDeviceEvent(kDisplaysChangedEvent,
                    [](Runtime & /*runtime*/, std::vector<Value> & /*args*/) {});
  });

  // Something dragged onto a view that said it would take one.

  setKeyListener([this](Tag tag, const KeyCombination &pressed) {
    emitDeviceEvent(kKeyEvent, [tag, pressed](Runtime &runtime, std::vector<Value> &args) {
      Object payload(runtime);
      payload.setProperty(runtime, "tag", Value(static_cast<int>(tag)));
      payload.setProperty(runtime, "key",
                          String::createFromUtf8(runtime, pressed.key));
      payload.setProperty(runtime, "altKey", Value(pressed.modifiers.alt));
      payload.setProperty(runtime, "ctrlKey", Value(pressed.modifiers.ctrl));
      payload.setProperty(runtime, "metaKey", Value(pressed.modifiers.meta));
      payload.setProperty(runtime, "shiftKey", Value(pressed.modifiers.shift));
      args.emplace_back(runtime, payload);
    });
  });
  setDropListener([this](const DropEvent &drop) {
    emitDeviceEvent(kDropEvent, [drop](Runtime &runtime, std::vector<Value> &args) {
      Object payload(runtime);
      payload.setProperty(runtime, "tag", Value(static_cast<int>(drop.tag)));
      payload.setProperty(
          runtime,
          "phase",
          String::createFromUtf8(runtime,
                                 drop.phase == DropPhase::Over      ? "over"
                                     : drop.phase == DropPhase::Leave ? "leave"
                                                                      : "drop"));
      payload.setProperty(runtime, "x", Value(drop.x));
      payload.setProperty(runtime, "y", Value(drop.y));

      // Only a drop carries contents; see core/DragAndDrop.h for why an
      // `over` deliberately does not.
      Array files(runtime, drop.payload.files.size());
      for (size_t i = 0; i < drop.payload.files.size(); i++) {
        files.setValueAtIndex(
            runtime, i, String::createFromUtf8(runtime, drop.payload.files[i]));
      }
      payload.setProperty(runtime, "files", files);
      payload.setProperty(runtime, "text",
                          String::createFromUtf8(runtime, drop.payload.text));

      args.emplace_back(std::move(payload));
    });
  });

  setHostQuitRequestListener([this]() {
    emitDeviceEvent(kQuitRequestedEvent,
                    [](Runtime & /*runtime*/, std::vector<Value> & /*args*/) {});
  });
}


DesktopWindowsModule::~DesktopWindowsModule() {
  DesktopWindowsModule *successor = nullptr;
  {
    const std::lock_guard<std::mutex> lock(liveMutex());
    std::vector<DesktopWindowsModule *> &live = liveModules();
    live.erase(std::remove(live.begin(), live.end(), this), live.end());
    if (!live.empty()) {
      // Newest, arbitrarily but consistently: any live one can emit, and the
      // newest is the one a fresh lookup would have produced anyway.
      successor = live.back();
    }
  }

  if (successor != nullptr) {
    // Handed over rather than cleared. The listeners capture a module, and this
    // one is going; clearing them would silence a platform that still has five
    // other modules serving, which is what happened to every keyboard shortcut
    // in an app until this was found.
    successor->installListeners();
    return;
  }

  // The last one out. Now they can go.
  setHostWindowClosedListener(nullptr);
  setHostWindowCloseRequestListener(nullptr);
  setHostQuitRequestListener(nullptr);
  setDisplaysListener(nullptr);
  setKeyListener(nullptr);
  setDropListener(nullptr);
}

Value DesktopWindowsModule::noop(Runtime & /*runtime*/,
                                 TurboModule & /*module*/,
                                 const Value * /*args*/,
                                 size_t /*count*/) {
  return Value::undefined();
}

Value DesktopWindowsModule::open(Runtime &runtime,
                                 TurboModule &module,
                                 const Value *args,
                                 size_t count) {
  NewWindowOptions options;
  if (count >= 1 && args[0].isObject()) {
    const Object given = args[0].asObject(runtime);
    options.component = stringProperty(runtime, given, "component");
    options.title = stringProperty(runtime, given, "title");
    options.width = numberProperty(runtime, given, "width", options.width);
    options.height = numberProperty(runtime, given, "height", options.height);

    const Value props = given.getProperty(runtime, "props");
    if (props.isObject()) {
      options.props = facebook::jsi::dynamicFromValue(runtime, props);
    }
  }

  auto promise = std::make_shared<facebook::react::AsyncPromise<folly::dynamic>>(
      runtime, static_cast<DesktopWindowsModule &>(module).jsInvoker_);

  // Onto the UI thread, where a window may be made, and back through the
  // promise. Blocking the JavaScript thread on a window manager is the thing
  // this shape exists to avoid.
  postToUiThread([promise, options = std::move(options)] {
    const facebook::react::SurfaceId opened = openHostWindow(options);
    // Null rather than zero for "could not": an id is a number, and a caller
    // checking `if (id)` would read a failure as window zero.
    promise->resolve(opened == 0 ? folly::dynamic(nullptr)
                                 : folly::dynamic(static_cast<int>(opened)));
  });

  return Value(runtime,
               facebook::react::bridging::toJs(
                   runtime, *promise, static_cast<DesktopWindowsModule &>(module).jsInvoker_));
}

Value DesktopWindowsModule::close(Runtime & /*runtime*/,
                                  TurboModule & /*module*/,
                                  const Value *args,
                                  size_t count) {
  if (count >= 1 && args[0].isNumber()) {
    const auto surfaceId = static_cast<facebook::react::SurfaceId>(args[0].asNumber());
    postToUiThread([surfaceId] { closeHostWindow(surfaceId); });
  }
  return Value::undefined();
}

Value DesktopWindowsModule::setHandledKeys(Runtime &runtime,
                                           TurboModule & /*module*/,
                                           const Value *args,
                                           size_t count) {
  if (count < 2 || !args[0].isNumber() || !args[1].isObject()) {
    return Value::undefined();
  }
  const auto tag = static_cast<Tag>(args[0].asNumber());
  Object list = args[1].getObject(runtime);
  if (!list.isArray(runtime)) {
    return Value::undefined();
  }
  Array combinations = list.getArray(runtime);
  const size_t length = combinations.length(runtime);

  std::vector<KeyCombination> claimed;
  claimed.reserve(length);
  for (size_t i = 0; i < length; i++) {
    Value entry = combinations.getValueAtIndex(runtime, i);
    if (!entry.isObject()) {
      continue;
    }
    Object combination = entry.getObject(runtime);
    Value key = combination.getProperty(runtime, "key");
    if (!key.isString()) {
      // A combination with no key is not one. Skipped rather than refused: an
      // app rebuilding its list should not lose the rest of it to one bad entry.
      continue;
    }
    const auto flag = [&](const char *name) {
      Value value = combination.getProperty(runtime, name);
      return value.isBool() && value.getBool();
    };
    claimed.push_back(KeyCombination{
        key.getString(runtime).utf8(runtime),
        KeyModifiers{flag("altKey"), flag("ctrlKey"), flag("metaKey"), flag("shiftKey")}});
  }

  // No hop, for the reason interceptClose gives: this is read on the UI thread
  // inside a key handler that cannot wait for the JavaScript thread, and it is a
  // vector behind a mutex rather than a toolkit call. Setting it late costs one
  // keystroke that was not claimed.
  basalt::setHandledKeys(tag, std::move(claimed));
  return Value::undefined();
}

Value DesktopWindowsModule::clearHandledKeys(Runtime & /*runtime*/,
                                             TurboModule & /*module*/,
                                             const Value *args,
                                             size_t count) {
  if (count >= 1 && args[0].isNumber()) {
    basalt::clearHandledKeys(static_cast<Tag>(args[0].asNumber()));
  }
  return Value::undefined();
}

Value DesktopWindowsModule::interceptClose(Runtime & /*runtime*/,
                                           TurboModule & /*module*/,
                                           const Value *args,
                                           size_t count) {
  if (count >= 1 && args[0].isNumber()) {
    const auto surfaceId = static_cast<facebook::react::SurfaceId>(args[0].asNumber());
    const bool intercepted = count >= 2 && args[1].isBool() && args[1].getBool();
    // No hop. This is read from inside a close handler on the UI thread, which
    // cannot wait for the JavaScript thread to get around to setting it -- and
    // it is a flag behind a mutex, not a toolkit call, so there is nothing to
    // marshal. Setting it late means one close that is not intercepted, which
    // is a window that closes; setting it on a hop could mean a close handler
    // reading a flag the app set several frames ago.
    setHostWindowCloseIntercepted(surfaceId, intercepted);
  }
  return Value::undefined();
}

namespace {

// One display, as JavaScript sees it. Flat rather than nested rectangles:
// `bounds` and `workArea` as objects would read better and would mean two
// more allocations per display per call, and this is read while an app is
// deciding where to put a window.
Object displayObject(Runtime &runtime, const DisplayInfo &info) {
  Object out(runtime);
  out.setProperty(runtime, "x", Value(info.x));
  out.setProperty(runtime, "y", Value(info.y));
  out.setProperty(runtime, "width", Value(info.width));
  out.setProperty(runtime, "height", Value(info.height));
  out.setProperty(runtime, "workX", Value(info.workX));
  out.setProperty(runtime, "workY", Value(info.workY));
  out.setProperty(runtime, "workWidth", Value(info.workWidth));
  out.setProperty(runtime, "workHeight", Value(info.workHeight));
  out.setProperty(runtime, "scaleFactor", Value(info.scaleFactor));
  out.setProperty(runtime, "primary", Value(info.primary));
  return out;
}

} // namespace

Value DesktopWindowsModule::getDisplays(Runtime &runtime,
                                        TurboModule & /*module*/,
                                        const Value * /*args*/,
                                        size_t /*count*/) {
  // Off the cache rather than the toolkit, so this needs no hop and can be
  // read during render -- the same arrangement getBounds has. The host
  // refreshes it at startup and on every change, so it is exact.
  const std::vector<DisplayInfo> found = lastKnownDisplays();

  Array result(runtime, found.size());
  for (size_t i = 0; i < found.size(); i++) {
    result.setValueAtIndex(runtime, i, displayObject(runtime, found[i]));
  }
  return result;
}

Value DesktopWindowsModule::getPointerPosition(Runtime &runtime,
                                               TurboModule &module,
                                               const Value * /*args*/,
                                               size_t /*count*/) {
  auto promise = std::make_shared<facebook::react::AsyncPromise<folly::dynamic>>(
      runtime, static_cast<DesktopWindowsModule &>(module).jsInvoker_);

  // A promise rather than a cached read, because there is nothing to cache:
  // the answer changes whenever the pointer moves and no host reports that.
  // So this is the one display question that has to go and ask.
  postToUiThread([promise] {
    const PointerPosition where = pointerPosition();
    folly::dynamic out = folly::dynamic::object;
    out["x"] = where.x;
    out["y"] = where.y;
    // So that an app can tell "at the origin" from "this desktop will not
    // say" -- which GTK never will; see GtkWindowControl.cpp.
    out["known"] = where.known;
    promise->resolve(std::move(out));
  });

  return Value(runtime,
               facebook::react::bridging::toJs(
                   runtime, *promise, static_cast<DesktopWindowsModule &>(module).jsInvoker_));
}

Value DesktopWindowsModule::quit(Runtime & /*runtime*/,
                                 TurboModule & /*module*/,
                                 const Value * /*args*/,
                                 size_t /*count*/) {
  // Hopped, unlike interceptQuit: this one ends the process through a
  // toolkit call, and those belong on the thread that owns the toolkit.
  postToUiThread([] { quitHost(); });
  return Value::undefined();
}

Value DesktopWindowsModule::interceptQuit(Runtime & /*runtime*/,
                                          TurboModule & /*module*/,
                                          const Value *args,
                                          size_t count) {
  // No hop, for the reason interceptClose gives: this is read on the UI
  // thread from inside a terminate handler that cannot wait for the
  // JavaScript thread, and it is a flag behind a mutex rather than a toolkit
  // call.
  setHostQuitIntercepted(count >= 1 && args[0].isBool() && args[0].getBool());
  return Value::undefined();
}

Value DesktopWindowsModule::getWindows(Runtime &runtime,
                                       TurboModule & /*module*/,
                                       const Value * /*args*/,
                                       size_t /*count*/) {
  // Read rather than hopped: this is bookkeeping the host keeps in a vector,
  // and an app asking during render cannot wait for a thread.
  const std::vector<facebook::react::SurfaceId> open = hostWindows();
  Array result(runtime, open.size());
  for (size_t i = 0; i < open.size(); i++) {
    result.setValueAtIndex(runtime, i, Value(static_cast<int>(open[i])));
  }
  return Value(runtime, result);
}

} // namespace basalt
