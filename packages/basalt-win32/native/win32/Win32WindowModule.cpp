#include "Win32WindowModule.h"

#include "WindowControl.h"
#include "WindowMethods.h"
#include "PlatformServices.h"
#include "Win32TitleBar.h"

#include <windows.h>

#include <cstdint>
#include <optional>
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

constexpr const char *kMetricsChangedEvent = "basaltTitleBarMetricsChanged";

// processColor's 0xAARRGGBB number, as the COLORREF DWM takes, or nullopt for
// anything that is not a number -- which is how JavaScript asks for the
// system's colour back. The alpha is dropped: a caption is opaque.
std::optional<COLORREF> colorArgument(const Value *args, size_t count, size_t index) {
  if (index >= count || !args[index].isNumber()) {
    return std::nullopt;
  }
  const auto argb = static_cast<uint32_t>(static_cast<int64_t>(args[index].asNumber()));
  return RGB((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF);
}

Value metricsValue(Runtime &runtime, const TitleBarMetrics &metrics) {
  Object object(runtime);
  object.setProperty(runtime, "height", static_cast<double>(metrics.height));
  object.setProperty(runtime, "buttonsWidth", static_cast<double>(metrics.buttonsWidth));
  object.setProperty(runtime,
                     "style",
                     String::createFromAscii(
                         runtime, metrics.style == TitleBarStyle::Hidden ? "hidden" : "native"));
  return Value(runtime, object);
}

} // namespace

Win32WindowModule::Win32WindowModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["setTitle"] = MethodMetadata{1, setTitle};
  methodMap_["setTitleBarColors"] = MethodMetadata{3, setTitleBarColors};
  methodMap_["setTitleBarStyle"] = MethodMetadata{1, setTitleBarStyle};
  methodMap_["getTitleBarMetrics"] = MethodMetadata{0, getTitleBarMetrics};
  // The caption's actions, under the names and in the order GtkWindowModule
  // and AppKitWindowModule already use. src/useWindow.ts calls each one
  // optionally -- NativeWindow?.minimize?.() -- so a host that leaves them out
  // does not fail, it silently does nothing, which is how these were missing
  // here while working on the other two desktops.
  methodMap_["minimize"] = MethodMetadata{0, minimize};
  methodMap_["toggleMaximize"] = MethodMetadata{0, toggleMaximize};
  methodMap_["close"] = MethodMetadata{0, closeWindow};
  methodMap_["startWindowDrag"] = MethodMetadata{0, startWindowDrag};
  // A window's geometry. The bodies are in core/WindowMethods.h, written once:
  // nothing about reading two numbers or shaping the answer differs per
  // desktop, and three copies would be three chances to drift.
  methodMap_["setSize"] = MethodMetadata{2, windowSetSize};
  methodMap_["setPosition"] = MethodMetadata{2, windowSetPosition};
  methodMap_["center"] = MethodMetadata{0, windowCenter};
  methodMap_["setFullScreen"] = MethodMetadata{1, windowSetFullScreen};
  // How big it may be, whether it may be resized at all, and whether it floats.
  // Not every desktop does every one of these; `getCapabilities` is how an app
  // finds out rather than guessing. See core/WindowControl.h.
  methodMap_["setMinimumSize"] = MethodMetadata{2, windowSetMinimumSize};
  methodMap_["setMaximumSize"] = MethodMetadata{2, windowSetMaximumSize};
  methodMap_["setResizable"] = MethodMetadata{1, windowSetResizable};
  methodMap_["setAlwaysOnTop"] = MethodMetadata{1, windowSetAlwaysOnTop};
  methodMap_["getCapabilities"] = MethodMetadata{0, windowGetCapabilities};
  methodMap_["getBounds"] = MethodMetadata{0, windowGetBounds};
  // What a NativeEventEmitter over this module calls; the events go out as
  // device events either way.
  methodMap_["addListener"] = MethodMetadata{1, noop};
  methodMap_["removeListeners"] = MethodMetadata{1, noop};

  // Cleared in the destructor, the same arrangement AppearanceModule has with
  // its colour scheme observer.
  titleBar().setMetricsListener([this](const TitleBarMetrics &metrics) {
    emitDeviceEvent(kMetricsChangedEvent, [metrics](Runtime &runtime, std::vector<Value> &args) {
      args.emplace_back(metricsValue(runtime, metrics));
    });
  });

  // The window's geometry, the same way the title bar's metrics go out: a
  // device event, so `useWindow()` can hand an app live bounds rather than a
  // snapshot it has to remember to refresh.
  setWindowBoundsListener([this](const WindowBounds &bounds) {
    emitDeviceEvent(kWindowBoundsEvent, [bounds](Runtime &runtime, std::vector<Value> &args) {
      Object object(runtime);
      object.setProperty(runtime, "x", bounds.x);
      object.setProperty(runtime, "y", bounds.y);
      object.setProperty(runtime, "width", bounds.width);
      object.setProperty(runtime, "height", bounds.height);
      object.setProperty(runtime, "fullScreen", bounds.fullScreen);
      object.setProperty(runtime, "maximized", bounds.maximized);
      args.emplace_back(Value(runtime, object));
    });
  });

  // And ask for the bounds once, now that something is listening. The host
  // primes the cache when its window appears, but this module is built lazily
  // -- the first time JavaScript asks for it -- so an app that mounts before
  // then would read a window of no size and never be told otherwise.
  postToUiThread([] { notifyWindowBoundsChanged(); });
}

Win32WindowModule::~Win32WindowModule() {
  titleBar().setMetricsListener(nullptr);
  setWindowBoundsListener(nullptr);
}

Value Win32WindowModule::setTitle(Runtime &runtime,
                                  TurboModule & /*module*/,
                                  const Value *args,
                                  size_t count) {
  std::optional<std::string> title;
  if (count > 0 && args[0].isString()) {
    title = args[0].getString(runtime).utf8(runtime);
  }
  postToUiThread([title = std::move(title)]() mutable { titleBar().setTitle(std::move(title)); });
  return Value::undefined();
}

Value Win32WindowModule::setTitleBarColors(Runtime & /*runtime*/,
                                           TurboModule & /*module*/,
                                           const Value *args,
                                           size_t count) {
  const auto caption = colorArgument(args, count, 0);
  const auto text = colorArgument(args, count, 1);
  const auto border = colorArgument(args, count, 2);
  postToUiThread([caption, text, border] { titleBar().setColors(caption, text, border); });
  return Value::undefined();
}

Value Win32WindowModule::setTitleBarStyle(Runtime &runtime,
                                          TurboModule & /*module*/,
                                          const Value *args,
                                          size_t count) {
  const bool hidden =
      count > 0 && args[0].isString() && args[0].getString(runtime).utf8(runtime) == "hidden";
  postToUiThread([hidden] {
    titleBar().setStyle(hidden ? TitleBarStyle::Hidden : TitleBarStyle::Native);
  });
  return Value::undefined();
}

Value Win32WindowModule::getTitleBarMetrics(Runtime &runtime,
                                            TurboModule & /*module*/,
                                            const Value * /*args*/,
                                            size_t /*count*/) {
  return metricsValue(runtime, titleBar().metrics());
}

// The four below are GtkWindowModule's bodies with the toolkit swapped: the
// work is one call on the UI thread, and the shape is identical on purpose, so
// that a reader comparing the three hosts sees the same thing three times
// rather than three arrangements of it.
Value Win32WindowModule::minimize(Runtime & /*runtime*/,
                                  TurboModule & /*module*/,
                                  const Value * /*args*/,
                                  size_t /*count*/) {
  postToUiThread([] { titleBar().minimize(); });
  return Value::undefined();
}

Value Win32WindowModule::toggleMaximize(Runtime & /*runtime*/,
                                        TurboModule & /*module*/,
                                        const Value * /*args*/,
                                        size_t /*count*/) {
  postToUiThread([] { titleBar().toggleMaximize(); });
  return Value::undefined();
}

Value Win32WindowModule::closeWindow(Runtime & /*runtime*/,
                                     TurboModule & /*module*/,
                                     const Value * /*args*/,
                                     size_t /*count*/) {
  postToUiThread([] { titleBar().close(); });
  return Value::undefined();
}

Value Win32WindowModule::startWindowDrag(Runtime & /*runtime*/,
                                         TurboModule & /*module*/,
                                         const Value * /*args*/,
                                         size_t /*count*/) {
  postToUiThread([] { titleBar().startDrag(); });
  return Value::undefined();
}

Value Win32WindowModule::noop(Runtime & /*runtime*/,
                              TurboModule & /*module*/,
                              const Value * /*args*/,
                              size_t /*count*/) {
  return Value::undefined();
}

} // namespace basalt
