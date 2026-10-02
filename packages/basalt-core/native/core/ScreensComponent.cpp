#include "ScreensComponent.h"

#include <react/renderer/core/propsConversions.h>

namespace facebook::react {

extern const char RNSScreenComponentName[] = "RNSScreen";

RNSScreenProps::RNSScreenProps(const PropsParserContext &context,
                               const RNSScreenProps &sourceProps,
                               const RawProps &rawProps)
    : ViewProps(context, sourceProps, rawProps),
      activityState(convertRawProp(context, rawProps, "activityState",
                                   sourceProps.activityState, -1.0f)) {
  // Only an explicit 0 hides it. The default is -1, which the library uses for
  // a screen it is not managing, and 1 is a screen mid-transition that still
  // has to be on screen to be transitioned.
  if (activityState == 0.0f) {
    yogaStyle.setDisplay(yoga::Display::None);
  }
}

extern const char RNSScreenStackComponentName[] = "RNSScreenStack";
extern const char RNSScreenContainerComponentName[] = "RNSScreenContainer";
extern const char RNSScreenNavigationContainerComponentName[] =
    "RNSScreenNavigationContainer";
extern const char RNSScreenContentWrapperComponentName[] = "RNSScreenContentWrapper";
extern const char RNSScreenStackHeaderConfigComponentName[] =
    "RNSScreenStackHeaderConfig";
extern const char RNSScreenStackHeaderSubviewComponentName[] =
    "RNSScreenStackHeaderSubview";

} // namespace facebook::react

namespace basalt {

DesktopScreensModule::DesktopScreensModule(
    std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  // Deliberately empty. See the header.
}

} // namespace basalt
