#include "ScreensComponent.h"

#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/core/propsConversions.h>

#include <string>

namespace facebook::react {

extern const char RNSScreenComponentName[] = "RNSScreen";

RNSScreenProps::RNSScreenProps(const PropsParserContext &context,
                               const RNSScreenProps &sourceProps,
                               const RawProps &rawProps)
    : ViewProps(context, sourceProps, rawProps),
      activityState(convertRawProp(context, rawProps, "activityState",
                                   sourceProps.activityState, -1.0f)) {
  // `transparentModal` and `containedTransparentModal` are the two that show
  // what is under them. Matching on the substring rather than listing both
  // means a presentation added upstream with the same word in it behaves the
  // way its name says.
  const auto presentation = convertRawProp(
      context, rawProps, "stackPresentation", std::string{}, std::string{});
  seeThrough = presentation.find("ransparent") != std::string::npos;

  // Only an explicit 0 hides it. The default is -1, which the library uses for
  // a screen it is not managing, and 1 is a screen mid-transition that still
  // has to be on screen to be transitioned.
  if (activityState == 0.0f) {
    yogaStyle.setDisplay(yoga::Display::None);
  }
}

extern const char RNSScreenContainerComponentName[] = "RNSScreenContainer";
extern const char RNSScreenNavigationContainerComponentName[] =
    "RNSScreenNavigationContainer";
extern const char RNSScreenContentWrapperComponentName[] = "RNSScreenContentWrapper";
extern const char RNSScreenStackHeaderConfigComponentName[] =
    "RNSScreenStackHeaderConfig";
extern const char RNSScreenStackHeaderSubviewComponentName[] =
    "RNSScreenStackHeaderSubview";

extern const char RNSScreenStackComponentName[] = "RNSScreenStack";

void RNSScreenStackShadowNode::layout(LayoutContext layoutContext) {
  ConcreteViewShadowNode::layout(layoutContext);

  const auto &children = getChildren();


  // The topmost screen that is not see-through. Everything below it is covered
  // and gets hidden; a transparent modal covers nothing, so the search keeps
  // going down past one.
  auto topOpaque = children.size();
  for (auto i = children.size(); i-- > 0;) {
    const auto *screen = dynamic_cast<const RNSScreenShadowNode *>(children[i].get());
    if (screen == nullptr || screen->getConcreteProps().seeThrough) {
      continue;
    }
    topOpaque = i;
    break;
  }
  if (topOpaque == children.size()) {
    return;
  }

  for (size_t i = 0; i < topOpaque; ++i) {
    auto *child = const_cast<ShadowNode *>(children[i].get());
    auto *layoutable = dynamic_cast<LayoutableShadowNode *>(child);
    if (layoutable == nullptr) {
      continue;
    }
    auto metrics = layoutable->getLayoutMetrics();
    if (metrics.displayType == DisplayType::None) {
      continue;
    }
    metrics.displayType = DisplayType::None;
    // The same pair YogaLayoutableShadowNode::layout uses to write a child's
    // metrics: a node is sealed after its commit and has to be told before it
    // is written to.
    child->ensureUnsealed();
    layoutable->setLayoutMetrics(metrics);
  }
}

} // namespace facebook::react

namespace basalt {

DesktopScreensModule::DesktopScreensModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  // Deliberately empty. See the header.
}

} // namespace basalt
