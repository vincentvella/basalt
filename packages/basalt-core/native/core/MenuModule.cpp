#include "MenuModule.h"

#include "MenuModel.h"
#include "PlatformServices.h"
#include "TestDialog.h"

#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

#include <folly/dynamic.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

namespace {

using facebook::jsi::Array;
using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

std::string stringProperty(Runtime &runtime, const Object &item, const char *name) {
  const Value value = item.getProperty(runtime, name);
  return value.isString() ? value.asString(runtime).utf8(runtime) : std::string{};
}

bool boolProperty(Runtime &runtime, const Object &item, const char *name, bool fallback) {
  const Value value = item.getProperty(runtime, name);
  return value.isBool() ? value.asBool() : fallback;
}

// A context menu's entries, which are a flatter thing than the application
// menu's: one level, no roles, no submenus. That is not a simplification -- a
// popup menu on all three desktops is a list, and the nesting an application
// menu has is what a menu *bar* is for.
std::vector<MenuEntry> entriesFrom(Runtime &runtime, const Value &value) {
  std::vector<MenuEntry> entries;
  if (!value.isObject() || !value.asObject(runtime).isArray(runtime)) {
    return entries;
  }
  const Array array = value.asObject(runtime).asArray(runtime);
  const size_t length = array.size(runtime);
  for (size_t i = 0; i < length; i++) {
    const Value element = array.getValueAtIndex(runtime, i);
    if (!element.isObject()) {
      continue;
    }
    const Object item = element.asObject(runtime);
    // `{separator: true}`, which reads better in JavaScript than the empty
    // label the platforms use. An item with no label is one too, so an app that
    // built its list by filtering does not have to notice.
    if (boolProperty(runtime, item, "separator", false)) {
      entries.push_back(MenuEntry::separator());
      continue;
    }
    MenuEntry entry;
    entry.label = stringProperty(runtime, item, "label");
    entry.enabled = boolProperty(runtime, item, "enabled", true);
    entry.shortcut = stringProperty(runtime, item, "shortcut");
    entries.push_back(std::move(entry));
  }
  return entries;
}

std::vector<MenuItemModel> itemsFrom(Runtime &runtime, const Array &array);

MenuItemModel itemFrom(Runtime &runtime, const Object &item) {
  MenuItemModel model;
  model.label = stringProperty(runtime, item, "label");
  model.role = stringProperty(runtime, item, "role");
  model.accelerator = stringProperty(runtime, item, "accelerator");

  const Value separator = item.getProperty(runtime, "separator");
  model.separator = separator.isBool() && separator.asBool();

  const Value enabled = item.getProperty(runtime, "enabled");
  // Absent means enabled: an app listing an item without saying meant to show
  // it, and defaulting the other way would grey out every menu ever written.
  model.enabled = !enabled.isBool() || enabled.asBool();

  const Value id = item.getProperty(runtime, "id");
  model.id = id.isNumber() ? static_cast<int>(id.asNumber()) : 0;

  const Value submenu = item.getProperty(runtime, "submenu");
  if (submenu.isObject() && submenu.asObject(runtime).isArray(runtime)) {
    model.submenu = itemsFrom(runtime, submenu.asObject(runtime).asArray(runtime));
  }
  return model;
}

std::vector<MenuItemModel> itemsFrom(Runtime &runtime, const Array &array) {
  std::vector<MenuItemModel> items;
  const size_t length = array.size(runtime);
  items.reserve(length);
  for (size_t i = 0; i < length; i++) {
    const Value entry = array.getValueAtIndex(runtime, i);
    if (entry.isObject()) {
      items.push_back(itemFrom(runtime, entry.asObject(runtime)));
    }
  }
  return items;
}

} // namespace

DesktopMenuModule::DesktopMenuModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["setApplicationMenu"] = MethodMetadata{1, setMenu};
  methodMap_["isSupported"] = MethodMetadata{0, isSupported};
  methodMap_["showContextMenu"] = MethodMetadata{3, showContextMenu};
  // What a NativeEventEmitter over this module calls; the events go out as
  // device events either way.
  methodMap_["addListener"] = MethodMetadata{1, noop};
  methodMap_["removeListeners"] = MethodMetadata{1, noop};
}

DesktopMenuModule::~DesktopMenuModule() {
  // The handler holds a pointer to this module's emitter. Cleared on the way
  // out, for the same reason the title bar clears its metrics listener.
  setApplicationMenu(MenuModel{}, nullptr);
}

Value DesktopMenuModule::setMenu(Runtime &runtime,
                                 TurboModule &module,
                                 const Value *args,
                                 size_t count) {
  MenuModel model;
  if (count >= 1 && args[0].isObject() && args[0].asObject(runtime).isArray(runtime)) {
    model = itemsFrom(runtime, args[0].asObject(runtime).asArray(runtime));
  }

  auto *self = static_cast<DesktopMenuModule *>(&module);
  // Installed on the UI thread, like everything else that touches a window:
  // this arrives on the JavaScript thread and an NSMenu or an HMENU may not be
  // built there.
  postToUiThread([self, model = std::move(model)] {
    setApplicationMenu(model, [self](int id) {
      self->emitDeviceEvent(kMenuChosenEvent,
                            [id](Runtime & /*runtime*/, std::vector<Value> &args) {
                              args.emplace_back(Value(id));
                            });
    });
  });
  return Value::undefined();
}

Value DesktopMenuModule::isSupported(Runtime & /*runtime*/,
                                     TurboModule & /*module*/,
                                     const Value * /*args*/,
                                     size_t /*count*/) {
  return Value(applicationMenuSupported());
}

Value DesktopMenuModule::noop(Runtime & /*runtime*/,
                              TurboModule & /*module*/,
                              const Value * /*args*/,
                              size_t /*count*/) {
  return Value::undefined();
}

Value DesktopMenuModule::showContextMenu(Runtime &runtime,
                                         TurboModule &module,
                                         const Value *args,
                                         size_t count) {
  MenuRequest request;
  if (count >= 1) {
    request.entries = entriesFrom(runtime, args[0]);
  }
  // Negative is "wherever the pointer is", which is what the seam already means
  // and what a menu opened from the keyboard wants. An app that passes nothing
  // gets that rather than the top-left corner.
  if (count >= 3 && args[1].isNumber() && args[2].isNumber()) {
    request.x = args[1].asNumber();
    request.y = args[2].asNumber();
  }

  auto jsInvoker = static_cast<DesktopMenuModule &>(module).jsInvoker_;
  auto promise =
      std::make_shared<facebook::react::AsyncPromise<folly::dynamic>>(runtime, jsInvoker);

  if (request.entries.empty()) {
    // Nothing to show. Resolved rather than rejected: an app whose menu came
    // out empty because everything was filtered away has not made a mistake,
    // and a rejection would make it handle one.
    promise->resolve(folly::dynamic(nullptr));
    return Value(runtime, facebook::react::bridging::toJs(runtime, *promise, jsInvoker));
  }

  // `presentMenu`, not `showMenu`: everything that would put a popup on screen
  // goes through the scripted answer, and here it is not merely tidiness. On
  // macOS `popUpMenuPositioningItem` runs the menu's own tracking loop on the
  // main thread, so a menu nobody dismisses stops an automated run where it
  // stands. See core/TestDialog.h.
  presentMenu(request, [promise](int index) {
    // Null for dismissed, which is the same shape the file dialogs answer a
    // cancel with: an index is a number, and a caller checking `if (index)`
    // would read entry zero as nothing.
    promise->resolve(index < 0 ? folly::dynamic(nullptr) : folly::dynamic(index));
  });

  return Value(runtime, facebook::react::bridging::toJs(runtime, *promise, jsInvoker));
}

} // namespace basalt
