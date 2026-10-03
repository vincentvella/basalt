#include "ScreensComponent.h"

#include <react/renderer/core/LayoutableShadowNode.h>
#include <react/renderer/core/propsConversions.h>

#include <algorithm>
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
extern const char RNSScreenStackHeaderSubviewComponentName[] =
    "RNSScreenStackHeaderSubview";

extern const char RNSScreenStackComponentName[] = "RNSScreenStack";
extern const char RNSScreenStackHeaderConfigComponentName[] =
    "RNSScreenStackHeaderConfig";

RNSScreenStackHeaderConfigProps::RNSScreenStackHeaderConfigProps(
    const PropsParserContext &context,
    const RNSScreenStackHeaderConfigProps &sourceProps,
    const RawProps &rawProps)
    : ViewProps(context, sourceProps, rawProps),
      hidden(convertRawProp(context, rawProps, "hidden", sourceProps.hidden, false)) {
  // Nothing is styled here on purpose. Setting `yogaStyle` from a props
  // constructor does not reach this node: the yoga node holds its own copy,
  // and the props the node ends up with are not always the ones built here.
  // The bar is shaped in `adopt()` and taken out of the layout in `layout()`,
  // which are the two places that work. See the header.
}

void RNSScreenStackHeaderConfigComponentDescriptor::adopt(ShadowNode &shadowNode) const {
  auto &layoutable = static_cast<YogaLayoutableShadowNode &>(shadowNode);
  const auto &props =
      static_cast<const RNSScreenStackHeaderConfigProps &>(*shadowNode.getProps());

  if (!props.hidden) {
    // Padding rather than a height, which also centres the title.
    //
    // Only `setSize`, `setPadding` and `setPositionType` reach the yoga node
    // from here, and a height set through `setSize` leaves the title against
    // the top edge: `alignItems` lives in the props, and the props are the
    // thing that does not arrive. So the bar is sized by what is in it plus a
    // margin, which lands within a few points of the 56 every toolkit uses and
    // centres a one-line title without needing to know how tall it is.
    layoutable.setPadding({.left = kHeaderPaddingX,
                           .top = kHeaderPaddingY,
                           .right = kHeaderPaddingX,
                           .bottom = kHeaderPaddingY});
  }

  ConcreteComponentDescriptor::adopt(shadowNode);
}

void RNSScreenStackHeaderConfigShadowNode::layout(LayoutContext layoutContext) {
  ConcreteViewShadowNode::layout(layoutContext);

  // Hidden, or with nothing to show. Both end the same way and both have to
  // happen here rather than through the style, for the reason the props
  // constructor gives.
  if (!getConcreteProps().hidden && !getChildren().empty()) {
    return;
  }
  auto metrics = getLayoutMetrics();
  if (metrics.displayType == DisplayType::None) {
    return;
  }
  metrics.displayType = DisplayType::None;
  setLayoutMetrics(metrics);
}

// See the header: the content wrapper fills the screen, so the bar sits on top
// of it until something moves it.
void RNSScreenShadowNode::layout(LayoutContext layoutContext) {
  ConcreteViewShadowNode::layout(layoutContext);

  float headerHeight = 0;
  for (const auto &child : getChildren()) {
    const auto *header =
        dynamic_cast<const RNSScreenStackHeaderConfigShadowNode *>(child.get());
    if (header == nullptr || header->getConcreteProps().hidden ||
        header->getLayoutMetrics().displayType == DisplayType::None) {
      continue;
    }
    headerHeight = header->getLayoutMetrics().frame.size.height;
    break;
  }
  if (headerHeight <= 0) {
    return;
  }

  for (const auto &child : getChildren()) {
    auto *content = const_cast<ShadowNode *>(child.get());
    if (dynamic_cast<const RNSScreenStackHeaderConfigShadowNode *>(content) != nullptr) {
      continue;
    }
    auto *layoutable = dynamic_cast<LayoutableShadowNode *>(content);
    if (layoutable == nullptr) {
      continue;
    }
    auto metrics = layoutable->getLayoutMetrics();
    if (metrics.displayType == DisplayType::None || metrics.frame.origin.y != 0) {
      continue;
    }
    metrics.frame.origin.y = headerHeight;
    metrics.frame.size.height =
        std::max(0.0f, metrics.frame.size.height - headerHeight);
    content->ensureUnsealed();
    layoutable->setLayoutMetrics(metrics);
  }
}

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
