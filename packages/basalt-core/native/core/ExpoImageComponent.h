// expo-image's view, as a Fabric component.
//
// An Expo module's *functions* are an object in `globalThis.expo.modules`; see
// core/ExpoModules.h. An Expo module's *views* are not. `requireNativeViewManager`
// asks `globalThis.expo.getViewConfig(moduleName)` for a static view config and
// registers an ordinary React Native component called
// `ViewManagerAdapter_<moduleName>` with it. From that point on it is a Fabric
// component like any other: a name, a props class, a shadow node, a descriptor
// in the registry, and a mounting peer on each platform.
//
// So this file is the second half of the Expo port, and the half that is not
// Expo-specific at all -- there is nothing here that expo-modules-core has to
// be present for, which is why it compiles unconditionally. What needs Expo is
// the view config, and that lives with the rest of the runtime.
//
// **The view config is the contract.** React Native filters props against
// `validAttributes` in JavaScript before they are ever diffed, so a prop absent
// from the config never reaches C++ -- silently. That is the same trap phase 26
// found with `shouldNotifyLoadEvents`, and here both sides are ours, so the
// list in ExpoRuntime and the parsing below have to be read together.
//
// What is supported is what this platform can draw: a source, a content fit, a
// tint. expo-image's placeholder, transition, blurhash, cache policy and SF
// Symbols are left out of the view config, so they are dropped in JavaScript
// and never travel. An app using them gets an image without them rather than an
// error, which is the honest outcome for a prop whose effect is decorative.

#pragma once

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>
#include <react/renderer/imagemanager/primitives.h>

namespace facebook::react {

extern const char ExpoImageComponentName[];

class ExpoImageProps final : public ViewProps {
 public:
  ExpoImageProps() = default;
  ExpoImageProps(const PropsParserContext &context,
                 const ExpoImageProps &sourceProps,
                 const RawProps &rawProps);

  // expo-image resolves `source` to an array before it reaches native, the same
  // shape React Native's <Image> uses, so the same conversion reads it.
  ImageSources sources{};

  // expo-image's `contentFit` is CSS object-fit rather than React Native's
  // `resizeMode`, but every value it has maps onto one this platform already
  // draws, so it is translated here rather than carried as a second vocabulary.
  ImageResizeMode contentFit{ImageResizeMode::Cover};

  SharedColor tintColor{};
};

// onLoadStart/onLoad/onError, matching the direct event types the view config
// declares. There is no onLoadEnd here because expo-image's JavaScript raises
// that one itself, from its own onLoad and onError handlers, and never listens
// for it natively. Not ImageEventEmitter: that one is bound to React
// Native's <Image> component and its payload shape, and expo-image's
// `onLoad` carries `{source: {url, width, height, mediaType}}` rather than
// `{source: {uri, ...}}`.
class ExpoImageEventEmitter : public ViewEventEmitter {
 public:
  using ViewEventEmitter::ViewEventEmitter;

  void onLoadStart() const;
  void onLoad(const ImageSource &source, double width, double height) const;
  void onError(const std::string &message) const;
};

using ExpoImageShadowNode =
    ConcreteViewShadowNode<ExpoImageComponentName, ExpoImageProps, ExpoImageEventEmitter>;

using ExpoImageComponentDescriptor = ConcreteComponentDescriptor<ExpoImageShadowNode>;

} // namespace facebook::react
