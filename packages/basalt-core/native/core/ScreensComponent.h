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
#include <react/renderer/core/LayoutContext.h>

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

  // Whether the screen beneath this one shows through it. True for the
  // `transparentModal` presentations and false for everything else, which is
  // what decides how far down the stack hides. Parsed from `stackPresentation`
  // rather than kept as the string, because that is the only question asked.
  bool seeThrough{false};
};

// The screen, which also has to get its content out from under the header.
//
// react-native-screens gives the content wrapper `StyleSheet.absoluteFill`, so
// it covers the whole screen and the header, which is an ordinary in-flow
// child, is painted on top of it. That is right on a platform whose navigation
// bar floats; here it means the first 56 points of every screen are behind the
// bar.
//
// The screen is the only node that can see both, so it moves the content down
// by the header's height after layout. `adopt()` cannot do it instead: it runs
// before the children are attached, measured by a probe that saw zero of them.
class RNSScreenShadowNode final
    : public ConcreteViewShadowNode<RNSScreenComponentName, RNSScreenProps,
                                    ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  void layout(LayoutContext layoutContext) override;
};
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

BASALT_SCREENS_VIEW(RNSScreenContainer)
BASALT_SCREENS_VIEW(RNSScreenNavigationContainer)
BASALT_SCREENS_VIEW(RNSScreenContentWrapper)
BASALT_SCREENS_VIEW(RNSScreenStackHeaderSubview)

#undef BASALT_SCREENS_VIEW

extern const char RNSScreenStackHeaderConfigComponentName[];

/**
 * What a header puts around its content.
 *
 * A bar is its title plus a margin rather than a fixed height, because the
 * margin is the part that can be set from where this is applied. See the
 * descriptor below.
 */
inline constexpr float kHeaderPaddingX = 16.0f;
inline constexpr float kHeaderPaddingY = 18.0f;

// The header bar.
//
// On iOS and Android this component configures a navigation bar that the
// platform draws, and most of its props describe that bar. Here there is no
// such bar, and the useful half of it arrives as views: react-navigation's
// native stack takes its non-iOS branch on every desktop, which renders the
// title, the back button and anything custom as `RNSScreenStackHeaderSubview`
// children rather than as strings for a toolbar to draw.
//
// So this is a row that lays those children out, and the props it reads are the
// ones that describe the row rather than the ones that describe UIKit.
//
// `title` as a plain string is the part that does not arrive as a view, and is
// not drawn: react-navigation only renders it as a `<Text>` when the title is
// centred or supplied as a component. See the navigation page in the docs.
class RNSScreenStackHeaderConfigProps final : public ViewProps {
 public:
  RNSScreenStackHeaderConfigProps() = default;
  RNSScreenStackHeaderConfigProps(const PropsParserContext &context,
                                  const RNSScreenStackHeaderConfigProps &sourceProps,
                                  const RawProps &rawProps);

  bool hidden{false};
};

// A header with nothing in it takes no space.
//
// react-navigation renders the title as a view only when it is centred or
// supplied as a component; a plain left-aligned string goes to the `title`
// prop, for a toolbar that does not exist here, and the config ends up with no
// children at all. Reserving 56 points for that would replace today's missing
// header with an empty grey bar, which is worse: at least nothing looks
// deliberate.
//
// Decided after layout because that is the first point at which the children
// are attached. `adopt()` runs before they are, measured rather than assumed.
class RNSScreenStackHeaderConfigShadowNode final
    : public ConcreteViewShadowNode<RNSScreenStackHeaderConfigComponentName,
                                    RNSScreenStackHeaderConfigProps,
                                    ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  void layout(LayoutContext layoutContext) override;
};
// The height is applied here rather than in the props.
//
// A props constructor can set `yogaStyle`, and for this node it does not take:
// the yoga node keeps its own copy of the style, and `setSize` is the
// affordance that writes to it and marks the node dirty. Measured, after a
// props-side version set the dimension and the bar still hugged its text.
// `ModalHostViewComponentDescriptor` upstream sizes itself the same way.
//
// Width is left alone. `points(NaN)` is `undefined` in yoga, so the bar still
// stretches to its parent rather than being pinned to a width nothing knows
// yet at adopt time.
class RNSScreenStackHeaderConfigComponentDescriptor final
    : public ConcreteComponentDescriptor<RNSScreenStackHeaderConfigShadowNode> {
 public:
  using ConcreteComponentDescriptor::ConcreteComponentDescriptor;

  void adopt(ShadowNode &shadowNode) const override;
};

extern const char RNSScreenStackComponentName[];

// The stack, which shows one screen and covers the rest.
//
// A native stack renders every screen it has pushed and expects the platform to
// show the top one. On iOS that is UINavigationController; `activityState` is
// not the signal, and measuring it is what established that: react-native-screens
// throws `activityState cannot be decreased in NativeStack`, and every screen in
// a pushed stack reports 2, the covered ones included.
//
// So the stack does it, after layout, by giving every screen below the topmost
// opaque one `DisplayType::None`. All three mounting managers already hide a
// view on exactly that, so this is one implementation rather than three, and a
// hidden widget is out of the accessibility tree on each of them for free.
//
// What this does not do is skip the layout. Yoga has already measured the
// covered screens by the time `layout()` can see which is on top, and the only
// way to get in front of that would be for a screen to know its own position in
// the stack, which it does not. The cost is measuring a subtree that is not
// shown; the thing that was actually wrong, two screens mounted and both
// reaching accessibility, is fixed.
class RNSScreenStackShadowNode final
    : public ConcreteViewShadowNode<RNSScreenStackComponentName, ViewProps,
                                    ViewEventEmitter> {
 public:
  using ConcreteViewShadowNode::ConcreteViewShadowNode;

  void layout(LayoutContext layoutContext) override;
};


using RNSScreenStackComponentDescriptor =
    ConcreteComponentDescriptor<RNSScreenStackShadowNode>;

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
