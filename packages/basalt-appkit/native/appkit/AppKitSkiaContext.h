// What @shopify/react-native-skia needs from this host.
//
// `RNSkPlatformContext` is the package's one seam: it hands Skia a runtime, a
// call invoker and this. The package ships an Apple implementation and basalt
// cannot use it -- not because its body is bridge-bound, which it almost is not,
// but because its *header* takes an `RCTBridge *` and builds a screenshot
// service from `bridge.uiManager`. So this is basalt's, and most of it delegates
// to `MetalContext`, which the package ships and which touches no React at all.
//
// Three of the seventeen overrides throw rather than work, each naming itself:
// the native-buffer trio, which is CVPixelBuffer interchange nothing here asks
// for, and the view screenshot, which needs a tag-to-view lookup this host has
// no reason to expose yet. A throw that says which call was not implemented is
// worth more than a plausible wrong answer -- `makeImageSnapshot` of a view
// returning a blank image would look like a rendering bug for a long time.

#pragma once

#include "RNSkPlatformContext.h"

#include <functional>
#include <memory>
#include <string>

namespace basalt {

// A pixel density, because Skia asks for one up front and AppKit's is per
// screen. The main screen's, read once; a window moved between displays of
// different scale is a thing to fix when something notices.
std::shared_ptr<RNSkia::RNSkPlatformContext>
makeSkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker);

} // namespace basalt
