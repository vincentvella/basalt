#include "SafeAreaComponent.h"

#include <jsi/jsi.h>

namespace facebook::react {

extern const char SafeAreaProviderComponentName[] = "RNCSafeAreaProvider";

void SafeAreaProviderEventEmitter::onInsetsChange(const Rect &frame,
                                                  const EdgeInsets &insets) const {
  dispatchEvent("insetsChange", [frame, insets](jsi::Runtime &runtime) {
    auto frameOut = jsi::Object(runtime);
    frameOut.setProperty(runtime, "x", frame.origin.x);
    frameOut.setProperty(runtime, "y", frame.origin.y);
    frameOut.setProperty(runtime, "width", frame.size.width);
    frameOut.setProperty(runtime, "height", frame.size.height);

    auto insetsOut = jsi::Object(runtime);
    insetsOut.setProperty(runtime, "top", insets.top);
    insetsOut.setProperty(runtime, "right", insets.right);
    insetsOut.setProperty(runtime, "bottom", insets.bottom);
    insetsOut.setProperty(runtime, "left", insets.left);

    auto payload = jsi::Object(runtime);
    payload.setProperty(runtime, "frame", std::move(frameOut));
    payload.setProperty(runtime, "insets", std::move(insetsOut));
    return payload;
  });
}

// Emitted on every layout that produces a real frame, rather than only on a
// change.
//
// A shadow node is cloned per commit, so there is nowhere here to remember what
// was sent last without carrying Fabric State for it, and State for a value the
// platform already knows would be machinery for its own sake. The deduplication
// that matters is already in the library: `setInsets` and `setFrame` both
// compare before they store, so an identical event is a React state write that
// bails out without re-rendering, and the loop this would otherwise be settles
// after one pass.
//
// The zero-size guard is not an optimisation. The first layout of a surface can
// produce an empty frame, and emitting that would unblock the provider's
// children against a 0x0 frame, which is worse than making them wait one more
// pass for the real one.
void SafeAreaProviderShadowNode::layout(LayoutContext layoutContext) {
  ConcreteViewShadowNode::layout(layoutContext);

  const auto frame = getLayoutMetrics().frame;
  if (frame.size.width <= 0 || frame.size.height <= 0) {
    return;
  }
  // Zero on every desktop: a window has no notch and no home indicator. See the
  // header for what would change that.
  getConcreteEventEmitter().onInsetsChange(frame, EdgeInsets{0, 0, 0, 0});
}

} // namespace facebook::react

namespace basalt {
namespace {

// Written once by the host thread before JavaScript runs and read from the JS
// thread afterwards, which is the ordering every host already has for its
// window. Plain doubles rather than an atomic pair: a torn read here would
// produce a slightly wrong first frame that the first `onInsetsChange`
// immediately replaces, and a mutex on a value read once is not worth it.
double gWindowWidth = 0;
double gWindowHeight = 0;

} // namespace

void setInitialWindowFrame(double width, double height) {
  gWindowWidth = width;
  gWindowHeight = height;
}

DesktopSafeAreaModule::DesktopSafeAreaModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["getConstants"] = MethodMetadata{0, getConstants};
}

facebook::jsi::Value DesktopSafeAreaModule::getConstants(
    facebook::jsi::Runtime &runtime,
    facebook::react::TurboModule & /*module*/,
    const facebook::jsi::Value * /*args*/,
    size_t /*count*/) {
  auto constants = facebook::jsi::Object(runtime);
  if (gWindowWidth <= 0 || gWindowHeight <= 0) {
    // Absent rather than zero. See setInitialWindowFrame.
    return constants;
  }

  auto insets = facebook::jsi::Object(runtime);
  insets.setProperty(runtime, "top", 0);
  insets.setProperty(runtime, "right", 0);
  insets.setProperty(runtime, "bottom", 0);
  insets.setProperty(runtime, "left", 0);

  auto frame = facebook::jsi::Object(runtime);
  frame.setProperty(runtime, "x", 0);
  frame.setProperty(runtime, "y", 0);
  frame.setProperty(runtime, "width", gWindowWidth);
  frame.setProperty(runtime, "height", gWindowHeight);

  auto metrics = facebook::jsi::Object(runtime);
  metrics.setProperty(runtime, "insets", std::move(insets));
  metrics.setProperty(runtime, "frame", std::move(frame));

  constants.setProperty(runtime, "initialWindowMetrics", std::move(metrics));
  return constants;
}

} // namespace basalt
