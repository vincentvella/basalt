#include "DialogModule.h"

#include "PlatformServices.h"
#include "TestDialog.h"

#include <ReactCommon/TurboModuleUtils.h>
#include <react/bridging/Bridging.h>
#include <react/bridging/Promise.h>

#include <folly/dynamic.h>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace basalt {

namespace {

using facebook::jsi::Object;
using facebook::jsi::Runtime;
using facebook::jsi::Value;
using facebook::react::TurboModule;

std::string stringProperty(Runtime &runtime, const Object &options, const char *name) {
  const Value value = options.getProperty(runtime, name);
  return value.isString() ? value.asString(runtime).utf8(runtime) : std::string{};
}

bool boolProperty(Runtime &runtime, const Object &options, const char *name) {
  const Value value = options.getProperty(runtime, name);
  return value.isBool() && value.asBool();
}

// `filters: [{name, extensions: ['png', 'jpg']}]`, which is Electron's shape
// and the one every desktop's own API is a rearrangement of.
std::vector<FileFilter> filtersFrom(Runtime &runtime, const Object &options) {
  std::vector<FileFilter> filters;
  const Value value = options.getProperty(runtime, "filters");
  if (!value.isObject() || !value.asObject(runtime).isArray(runtime)) {
    return filters;
  }
  const auto array = value.asObject(runtime).asArray(runtime);
  const size_t length = array.size(runtime);
  for (size_t i = 0; i < length; i++) {
    const Value entry = array.getValueAtIndex(runtime, i);
    if (!entry.isObject()) {
      continue;
    }
    const Object filter = entry.asObject(runtime);
    FileFilter parsed;
    parsed.name = stringProperty(runtime, filter, "name");

    const Value extensions = filter.getProperty(runtime, "extensions");
    if (extensions.isObject() && extensions.asObject(runtime).isArray(runtime)) {
      const auto list = extensions.asObject(runtime).asArray(runtime);
      const size_t count = list.size(runtime);
      for (size_t j = 0; j < count; j++) {
        const Value extension = list.getValueAtIndex(runtime, j);
        if (extension.isString()) {
          parsed.extensions.push_back(extension.asString(runtime).utf8(runtime));
        }
      }
    }
    // A filter with no extensions matches nothing and would be a trap on a
    // desktop that shows it anyway. Dropped rather than shown.
    if (!parsed.extensions.empty()) {
      filters.push_back(std::move(parsed));
    }
  }
  return filters;
}

FileDialogRequest requestFrom(Runtime &runtime,
                              FileDialogRequest::Kind kind,
                              const Value *args,
                              size_t count) {
  FileDialogRequest request;
  request.kind = kind;
  if (count == 0 || !args[0].isObject()) {
    return request;
  }
  const Object options = args[0].asObject(runtime);
  request.title = stringProperty(runtime, options, "title");
  request.defaultPath = stringProperty(runtime, options, "defaultPath");
  request.confirmLabel = stringProperty(runtime, options, "confirmLabel");
  request.multiple = kind == FileDialogRequest::Kind::OpenFile &&
      boolProperty(runtime, options, "multiple");
  request.filters = filtersFrom(runtime, options);
  return request;
}

// The one shape all three answer with. `paths` is a list even for a save, so
// that an app moving between them is not also moving between result types; see
// the header.
Value present(Runtime &runtime,
              const std::shared_ptr<facebook::react::CallInvoker> &jsInvoker,
              FileDialogRequest request) {
  auto promise =
      std::make_shared<facebook::react::AsyncPromise<folly::dynamic>>(runtime, jsInvoker);

  presentFileDialog(request, [promise](bool canceled, const std::vector<std::string> &paths) {
    folly::dynamic list = folly::dynamic::array;
    for (const std::string &path : paths) {
      list.push_back(path);
    }
    promise->resolve(folly::dynamic::object("canceled", canceled)("paths", std::move(list)));
  });

  return Value(runtime, facebook::react::bridging::toJs(runtime, *promise, jsInvoker));
}

} // namespace

DesktopDialogModule::DesktopDialogModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["openFile"] = MethodMetadata{1, openFile};
  methodMap_["saveFile"] = MethodMetadata{1, saveFile};
  methodMap_["openFolder"] = MethodMetadata{1, openFolder};
}

Value DesktopDialogModule::openFile(Runtime &runtime,
                                    TurboModule &module,
                                    const Value *args,
                                    size_t count) {
  return present(runtime,
                 static_cast<DesktopDialogModule &>(module).jsInvoker_,
                 requestFrom(runtime, FileDialogRequest::Kind::OpenFile, args, count));
}

Value DesktopDialogModule::saveFile(Runtime &runtime,
                                    TurboModule &module,
                                    const Value *args,
                                    size_t count) {
  return present(runtime,
                 static_cast<DesktopDialogModule &>(module).jsInvoker_,
                 requestFrom(runtime, FileDialogRequest::Kind::SaveFile, args, count));
}

Value DesktopDialogModule::openFolder(Runtime &runtime,
                                      TurboModule &module,
                                      const Value *args,
                                      size_t count) {
  return present(runtime,
                 static_cast<DesktopDialogModule &>(module).jsInvoker_,
                 requestFrom(runtime, FileDialogRequest::Kind::OpenFolder, args, count));
}

} // namespace basalt
