// react-native-safe-area-context's provider view, as a Fabric component.
//
// ## Why this is the file that makes navigation work
//
// `SafeAreaProvider` renders `insets != null ? children : null`, and `insets`
// starts null unless the application passed `initialMetrics`. The only other
// thing that fills it is an `onInsetsChange` event from this component. With no
// native half, nothing ever fires it, so the provider renders null forever and
// every screen beneath it stays unmounted.
//
// That is why an app using react-navigation or expo-router showed an empty
// window and logged nothing: there is no error anywhere in that path, just a
// provider waiting for a platform that never answers. Measured on 2026-10-01
// before this existed; see docs/backlog/ecosystem.md.
//
// ## Why the event comes from layout rather than from a host
//
// Insets on a phone are the notch and the home indicator, which the toolkit
// knows and C++ does not. A desktop window has neither, so the insets are zero
// on all three platforms and the only unknown is the frame, which is this
// view's own layout. Both are therefore available here, in the shadow node,
// and a per-host mounting peer would be three copies of the same zero.
//
// The provider is styled `flex: 1` by the library, so its frame is the surface,
// which is what `SafeAreaFrameContext` is supposed to carry.
//
// If a desktop later grows a real inset -- a client-side-decorated title bar
// drawn over the content, say -- this is the place it would come from, and it
// would need a host behind it at that point.

#pragma once

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/core/LayoutContext.h>
#include <react/renderer/graphics/Rect.h>
#include <ReactCommon/TurboModule.h>

namespace facebook::react {

extern const char SafeAreaProviderComponentName[];

// `onInsetsChange`, whose payload is `{frame: {x, y, width, height}, insets:
// {top, right, bottom, left}}`. The names are the library's and an app reads
// them straight off `event.nativeEvent`, so they are a contract rather than a
// choice.
class SafeAreaProviderEventEmitter : public ViewEventEmitter {
 public:
  using ViewEventEmitter::ViewEventEmitter;

  void onInsetsChange(const Rect &frame, const EdgeInsets &insets) const;
};

// No props of its own. `onInsetsChange` is an event rather than a prop, and
// everything else the library sets on it is ordinary view styling, so
// `ViewProps` parses the lot.
class SafeAreaProviderShadowNode final
    : public ConcreteViewShadowNode<SafeAreaProviderComponentName, ViewProps,
                                    SafeAreaProviderEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  static ShadowNodeTraits BaseTraits() {
    return ConcreteViewShadowNode::BaseTraits();
  }

  void layout(LayoutContext layoutContext) override;
};

using SafeAreaProviderComponentDescriptor =
    ConcreteComponentDescriptor<SafeAreaProviderShadowNode>;

} // namespace facebook::react

namespace basalt {

// The window the application started with, which a host reports once it has
// one. Zero means not yet known, and the module then omits
// `initialWindowMetrics` rather than claiming a zero-sized window: the field is
// optional in the library's own spec, and absent is a state it already handles.
void setInitialWindowFrame(double width, double height);

// `RNCSafeAreaContext`, which the library asks for with `TurboModuleRegistry.get`.
//
// A null answer is survivable -- that is `get` rather than `getEnforcing` -- and
// until now that is what it got, which is why the warning appeared in every log.
// What the module is actually for is `initialWindowMetrics`, the export an app
// passes to `SafeAreaProvider` so the first frame is right rather than corrected
// one pass later. Nothing else in the library needs it.
class DesktopSafeAreaModule : public facebook::react::TurboModule {
 public:
  static constexpr auto kModuleName = "RNCSafeAreaContext";

  explicit DesktopSafeAreaModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);

 private:
  static facebook::jsi::Value getConstants(facebook::jsi::Runtime &runtime,
                                           facebook::react::TurboModule &module,
                                           const facebook::jsi::Value *args,
                                           size_t count);
};

} // namespace basalt
