// react-native-screens, as much of it as a desktop needs.
//
// ## What this is for
//
// react-navigation's native stack renders every screen it has ever pushed and
// relies on the platform to show one of them. The signal is `activityState` on
// each `RNSScreen`: 0 for a screen that is behind another, 2 for the one in
// front. With no native half, every screen is mounted and laid out at full
// size, one painted over the other. It looks right and is not: both are in the
// tree, both are measured, and a screen reader reads both.
//
// Measured before this existed, pushing Details over Home left tags 18 and 38
// side by side in the dumped tree, both 900x700. See docs/backlog/ecosystem.md.
//
// ## Why `display: none` rather than a mounting peer
//
// The platforms react-native-screens was written for detach the inactive
// screen's view from the container, which is a mounting-layer operation and
// would be three implementations of it here. Yoga already has the concept:
// `display: none` takes a subtree out of layout entirely, and every host here
// already honours it, so an inactive screen stops being laid out and stops
// being painted without any host learning the word "screen".
//
// The difference from a real detach is that the shadow node still exists, which
// costs a node per background screen and nothing else. What it buys is that the
// behaviour is identical on all three desktops for free.
//
// ## What is deliberately not here
//
// The header. `RNSScreenStackHeaderConfig` carries a title, a back button and a
// tint, and drawing it means a real toolkit header per platform rather than a
// prop translation. It is registered as a plain view so that it mounts and
// takes no space, which is what it already did. An app that wants a title bar
// today draws its own; see docs/backlog/ecosystem.md.

#pragma once

#include <ReactCommon/TurboModule.h>
#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>

namespace facebook::react {

extern const char RNSScreenComponentName[];

class RNSScreenProps final : public ViewProps {
 public:
  RNSScreenProps() = default;
  RNSScreenProps(const PropsParserContext &context,
                 const RNSScreenProps &sourceProps,
                 const RawProps &rawProps);

  // 0 inactive, 1 transitioning, 2 active, and -1 for "the library is not
  // managing this one", which is its own default and has to stay visible.
  float activityState{-1.0f};
};

using RNSScreenShadowNode =
    ConcreteViewShadowNode<RNSScreenComponentName, RNSScreenProps, ViewEventEmitter>;
using RNSScreenComponentDescriptor = ConcreteComponentDescriptor<RNSScreenShadowNode>;

// The containers. Each is a plain view that holds screens, so ViewProps parses
// all of it and the only thing each needs of its own is a name. A name Fabric
// does not know becomes UnimplementedNativeView, which mounts but carries none
// of the props, so registering them is what makes the stack lay out at all.
#define BASALT_SCREENS_VIEW(Symbol)                                           \
  extern const char Symbol##ComponentName[];                                  \
  using Symbol##ShadowNode =                                                  \
      ConcreteViewShadowNode<Symbol##ComponentName, ViewProps, ViewEventEmitter>; \
  using Symbol##ComponentDescriptor = ConcreteComponentDescriptor<Symbol##ShadowNode>;

BASALT_SCREENS_VIEW(RNSScreenStack)
BASALT_SCREENS_VIEW(RNSScreenContainer)
BASALT_SCREENS_VIEW(RNSScreenNavigationContainer)
BASALT_SCREENS_VIEW(RNSScreenContentWrapper)
BASALT_SCREENS_VIEW(RNSScreenStackHeaderConfig)
BASALT_SCREENS_VIEW(RNSScreenStackHeaderSubview)

#undef BASALT_SCREENS_VIEW

} // namespace facebook::react

namespace basalt {

// `RNSModule`, whose TypeScript spec is `interface Spec extends TurboModule {}`:
// no methods at all. The library only asks whether it is there, and answering
// null is what produced the warning in every log. Nothing calls anything on it.
class DesktopScreensModule : public facebook::react::TurboModule {
 public:
  static constexpr auto kModuleName = "RNSModule";

  explicit DesktopScreensModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker);
};

} // namespace basalt
