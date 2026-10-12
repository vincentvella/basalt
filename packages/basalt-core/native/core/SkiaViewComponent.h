// `SkiaView`, which is what a `<Canvas>` from react-native-skia 3.x mounts.
//
// The 2.x one is `SkiaPictureView`, in core/SkiaPictureViewComponent.h beside
// this, and the rename is the whole difference as far as a component registry
// is concerned: `lib/module/specs/SkiaViewNativeComponent.js` is
// `codegenNativeComponent("SkiaView")` where it used to say the longer name.
// Both are registered, because which one an app mounts is decided by the Skia
// it installed and a host binary does not know which that is until the app's
// JavaScript runs.
//
// Plain `ViewProps`, for the reason the 2.x header spells out at length: a
// `<Canvas>` does not carry its content in props. It records in JavaScript and
// pushes the result through the Skia JSI API keyed by an id, and the only thing
// the native side needs from props is which id it is -- `nativeID`, an ordinary
// `ViewProps` field that arrives whatever a view config says.
//
// What is dropped, which is a shorter list than 2.x's: `opaque`,
// `highBitDepth`, `androidSurfaceType`, `androidZOrderOnTop` and
// `pointerEvents`. The two Android ones select a backing view there and mean
// nothing to a CAMetalLayer. `opaque` and `highBitDepth` are the backing
// store's format -- an opaque layer and ten bits a channel -- and a canvas
// without them draws the same picture into an eight-bit one. `pointerEvents`
// is already a `ViewProps` field and is handled where every other view's is.
// When one is wanted it belongs in a props class here and in the peer, which
// is the same call core/ExpoImageComponent.h makes about blurhash.
//
// Registered unconditionally, like the 2.x one and like ExpoImage: it costs a
// name in a registry, and the alternative is a host that has to be rebuilt to
// run an app that draws. Without Skia in the build the view mounts and stays
// empty -- and an app that draws has already failed at
// `getEnforcing('RNSkiaModule')` long before, which is the error that says
// what is actually missing.

#pragma once

#include <react/renderer/components/view/ConcreteViewShadowNode.h>
#include <react/renderer/components/view/ViewEventEmitter.h>
#include <react/renderer/components/view/ViewProps.h>
#include <react/renderer/core/ConcreteComponentDescriptor.h>

namespace facebook::react {

extern const char SkiaViewComponentName[];

using SkiaViewShadowNode =
    ConcreteViewShadowNode<SkiaViewComponentName, ViewProps, ViewEventEmitter>;

using SkiaViewComponentDescriptor = ConcreteComponentDescriptor<SkiaViewShadowNode>;

} // namespace facebook::react
