#include "ExpoImageComponent.h"

#include <react/renderer/core/graphicsConversions.h>
#include <react/renderer/core/propsConversions.h>
#include <react/renderer/components/image/conversions.h>

namespace facebook::react {

// The name React Native registers for it, from `requireNativeViewManager`:
// `ViewManagerAdapter_` plus the Expo module's name. There is no app-identifier
// suffix because nothing here sets `__expo_app_identifier__`, which exists so
// that two copies of a library in one app do not collide.
extern const char ExpoImageComponentName[] = "ViewManagerAdapter_ExpoImage";

namespace {

ImageResizeMode resizeModeFrom(const std::string &contentFit) {
  // CSS object-fit, which is what expo-image's contentFit is.
  if (contentFit == "contain") {
    return ImageResizeMode::Contain;
  }
  if (contentFit == "fill") {
    return ImageResizeMode::Stretch;
  }
  if (contentFit == "none") {
    return ImageResizeMode::None;
  }
  // `scale-down` is `contain` that never scales up. Nothing here can express
  // the second half, and contain is the half that matters for an image larger
  // than its box, which is the case scale-down exists for.
  if (contentFit == "scale-down") {
    return ImageResizeMode::Contain;
  }
  return ImageResizeMode::Cover;
}

} // namespace

ExpoImageProps::ExpoImageProps(const PropsParserContext &context,
                               const ExpoImageProps &sourceProps,
                               const RawProps &rawProps)
    : ViewProps(context, sourceProps, rawProps),
      sources(convertRawProp(context, rawProps, "source", sourceProps.sources, {})),
      tintColor(convertRawProp(context, rawProps, "tintColor", sourceProps.tintColor, {})) {
  const std::string fit =
      convertRawProp(context, rawProps, "contentFit", std::string{}, std::string{});
  contentFit = fit.empty() ? sourceProps.contentFit : resizeModeFrom(fit);
}

void ExpoImageEventEmitter::onLoadStart() const {
  dispatchEvent("loadStart");
}

// expo-image's payload, not React Native's: `{source: {url, width, height,
// mediaType}}`. An app reads `event.source.width`, so the names matter.
void ExpoImageEventEmitter::onLoad(const ImageSource &source, double width, double height) const {
  dispatchEvent("load", [uri = source.uri, width, height](jsi::Runtime &runtime) {
    auto loaded = jsi::Object(runtime);
    loaded.setProperty(runtime, "url", uri);
    loaded.setProperty(runtime, "width", width);
    loaded.setProperty(runtime, "height", height);
    loaded.setProperty(runtime, "mediaType", jsi::Value::null());
    loaded.setProperty(runtime, "isAnimated", false);

    auto payload = jsi::Object(runtime);
    payload.setProperty(runtime, "source", std::move(loaded));
    return payload;
  });
}

void ExpoImageEventEmitter::onError(const std::string &message) const {
  dispatchEvent("error", [message](jsi::Runtime &runtime) {
    auto payload = jsi::Object(runtime);
    payload.setProperty(runtime, "error", message);
    return payload;
  });
}

} // namespace facebook::react
