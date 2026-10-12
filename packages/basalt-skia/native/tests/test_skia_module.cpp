// `RNSkiaModule`'s `install`, against a real JavaScript runtime.
//
// This is the one test that can say Skia's JavaScript API arrives, because
// arriving is all `install` does: constructing an `RNSkManager` puts
// `SkiaApi` and the rest onto the global object as a side effect, and the
// package's `NativeSetup.js` reads them immediately afterwards. A module that
// returned true and installed nothing would pass a link check, pass a
// constructor check, and leave the app dead on the next line.
//
// So a Hermes runtime is built here and the module's own method table is
// called through -- the same table React Native's registry calls -- and the
// globals are read back out of the runtime afterwards.

#include "TestHarness.h"

#include "BasaltSkiaModule.h"
#include "JsRuntimeAccess.h"

#include <hermes/hermes.h>

#include <ReactCommon/TurboModule.h>

#include <memory>
#include <sstream>
#include <string>

namespace {

// What the registry does with a name, which is the half of the contract that
// is not install(): a package factory is asked about every module name in the
// app and must answer null for the ones that are not its own.
std::shared_ptr<facebook::react::TurboModule> ask(const std::string &name) {
  return basalt::makeSkiaTurboModule(name, basalt::jsCallInvoker());
}

// `install`, the way JavaScript reaches it: `TurboModule::get` with the
// property name, which answers a host function built from the method table.
// Calling the private static directly would skip the table, and a method
// whose metadata was never filled in is a module that exists and does
// nothing -- which is the mistake worth catching here.
facebook::jsi::Value callInstall(facebook::jsi::Runtime &runtime,
                                 facebook::react::TurboModule &module) {
  facebook::jsi::Value method =
      module.get(runtime, facebook::jsi::PropNameID::forAscii(runtime, "install"));
  EXPECT(method.isObject() && method.getObject(runtime).isFunction(runtime));
  if (!method.isObject() || !method.getObject(runtime).isFunction(runtime)) {
    return facebook::jsi::Value::undefined();
  }
  return method.getObject(runtime).getFunction(runtime).call(runtime);
}

bool hasGlobal(facebook::jsi::Runtime &runtime, const char *name) {
  return !runtime.global().getProperty(runtime, name).isUndefined();
}

} // namespace

TEST(skia_module_answers_only_to_its_own_name) {
  EXPECT(ask("RNSkiaModule") != nullptr);
  EXPECT(ask("SkiaModule") == nullptr);
  EXPECT(ask("") == nullptr);
  // The name React Native will actually ask with, spelled once in the header
  // and checked here against the literal the package's spec uses.
  EXPECT_EQ(std::string(basalt::BasaltSkiaModule::kModuleName), std::string("RNSkiaModule"));
}

TEST(skia_module_installs_skias_javascript_api) {
  std::unique_ptr<facebook::jsi::Runtime> runtime = facebook::hermes::makeHermesRuntime();
  EXPECT(runtime != nullptr);
  if (runtime == nullptr) {
    return;
  }

  // Nothing of Skia's before install, which is what makes the assertions
  // after it mean anything.
  EXPECT(!hasGlobal(*runtime, "SkiaApi"));

  std::shared_ptr<facebook::react::TurboModule> module = ask("RNSkiaModule");
  EXPECT(module != nullptr);
  if (module == nullptr) {
    return;
  }

  const facebook::jsi::Value result = callInstall(*runtime, *module);
  // Exactly `true`, not merely truthy: NativeSetup.js compares with `!==
  // true` and throws on anything else, so a module answering 1 or "ok" would
  // end the app with "failed to correctly install JSI Bindings".
  EXPECT(result.isBool());
  EXPECT(result.isBool() && result.getBool());

  // The manager, which is what a `<Canvas>` will register itself with.
  EXPECT(basalt::BasaltSkiaModule::manager() != nullptr);

  // And the globals the package's JavaScript reads on the next line.
  EXPECT(hasGlobal(*runtime, "SkiaApi"));

  // Twice is not twice. The package calls install from module scope and React
  // Native may evaluate that more than once; a second manager over the same
  // runtime would install a second set of bindings over the first.
  RNSkia::RNSkManager *first = basalt::BasaltSkiaModule::manager();
  const facebook::jsi::Value again = callInstall(*runtime, *module);
  EXPECT(again.isBool() && again.getBool());
  EXPECT(basalt::BasaltSkiaModule::manager() == first);
}
