// The AppKit half of a `<Canvas>`: a Metal layer inside an ordinary view.
//
// The package's SkiaUIView is an RCTViewComponentView that does three things --
// creates a drawing view, adds its layer as a sublayer of its own, and registers
// it with RNSkManager under the nativeId. None of the three needs React Native;
// what needed React Native was being an RCTViewComponentView. Here the view is
// already an `RnAppKitView` that the mounting manager made, so this is the three
// things and nothing else.
//
// A sublayer rather than a view of its own, exactly as the package does it: the
// Skia layer draws itself on the GPU and takes no part in layout, hit testing or
// the view tree. Making it a view would put it in all three.

#pragma once

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/mounting/ShadowView.h>

@class RnAppKitView;

namespace basalt {

// Gives `view` a Skia canvas if `shadowView` is a SkiaPictureView, updating its
// size if it already has one. False when the component is something else, so the
// caller can treat this as a filter rather than asking twice.
//
// Safe to call before `install` has run: without a manager there is nothing to
// register with, and it does nothing and says so in the log rather than creating
// a drawing view that no picture could ever reach.
bool applySkiaCanvas(RnAppKitView *view, const facebook::react::ShadowView &shadowView);

// Unregisters and releases the canvas for `tag`, if it had one. Called when the
// mounting walk forgets a tag, which is the one moment a view is known to be
// finished with.
void forgetSkiaCanvas(facebook::react::Tag tag);

} // namespace basalt
