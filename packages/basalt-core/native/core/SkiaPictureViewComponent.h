// `SkiaPictureView`, which is what a `<Canvas>` from @shopify/react-native-skia
// mounts.
//
// Plain `ViewProps`, unlike core/ExpoImageComponent.h which needed its own props
// class -- and for a reason worth stating, because it looks like a shortcut and
// is not. A `<Canvas>` does not carry its content in props. It renders a
// `SkPicture` in JavaScript and pushes it through the Skia JSI API keyed by an
// id, and the only thing the native side needs from props is which id it is:
//
//     <SkiaPictureViewNativeComponent nativeID={`${nativeId}`} ... />
//
// `nativeID` is an ordinary `ViewProps` field, so it arrives on any component
// whatever its view config says. That sidesteps the trap ExpoImageComponent.h
// spends a paragraph on: a prop absent from the JavaScript view config is
// dropped before it is ever diffed, silently. Here there is nothing to drop.
//
// What *is* dropped: `debug`, `opaque`, `colorSpace` and `highBitDepth`, the four
// props the package's spec declares. They are decoration -- an FPS overlay, an
// opaque backing, P3, ten bits a channel -- and a canvas without them draws the
// same picture. The same call ExpoImage makes about placeholders and blurhash.
// When one is wanted, it belongs in a props class here and in the peer.
//
// Registered unconditionally, like ExpoImage and for the same reason: it costs a
// name in a registry, and the alternative is a host that has to be rebuilt to
// run an app that draws. Without Skia in the build the view mounts and stays
// empty -- but an app that draws will already have failed at
// `getEnforcing('RNSkiaModule')` long before, which is the error that says what
// is actually missing.

#pragma once

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>

namespace facebook::react {

extern const char SkiaPictureViewComponentName[];

using SkiaPictureViewShadowNode =
    ConcreteViewShadowNode<SkiaPictureViewComponentName, ViewProps, ViewEventEmitter>;

using SkiaPictureViewComponentDescriptor =
    ConcreteComponentDescriptor<SkiaPictureViewShadowNode>;

} // namespace facebook::react
