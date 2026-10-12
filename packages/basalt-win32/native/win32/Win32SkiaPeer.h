// The Win32 half of a `<Canvas>`: a surface drawn in the paint walk.
//
// ## Two implementations, one pair of functions
//
// Both generations of react-native-skia answer to the two functions at the
// bottom of this file, and they are never both linked: the 2.x one is
// `Win32SkiaPeer.cpp` beside this header, compiled when core's cmake/Skia.cmake
// found a 2.x install, and the 3.x one is basalt-skia's
// `win32/BasaltSkiaCanvasWin32.cpp`, compiled when it found a 3.x one.
// cmake/Skia.cmake refuses a 3.x install by version, so exactly one definition
// of each exists. Win32MountingManager.cpp calls them under a guard that
// covers either, the way the AppKit one does -- see AppKitSkiaPeer.h, which is
// this arrangement on the other host.
//
// They differ in the component they answer to, which is 3.x's rename: a 2.x
// `<Canvas>` mounts a `SkiaPictureView` and a 3.x one a `SkiaView`. Both
// descriptors are registered on all three hosts.
//
// What follows describes the 2.x one. The 3.x one reaches the same place by a
// different route -- it renders on the GPU through Dawn and D3D12 and reads
// the frame back, rather than rasterising on the CPU -- and for the same
// reason, which is the next paragraph. BasaltSkiaCanvasWin32.h has its half.
//
// The other two hosts give the package a layer of its own -- a CAMetalLayer on
// macOS, a GL surface on GTK -- and let it present straight to the compositor.
// This host has neither a layer tree nor a view that is a window (see the note
// at the top of RnWin32View.h for why), so a canvas cannot present itself. What
// it can do is render into memory and let the one Direct2D walk that draws
// everything else draw it too, at the rect the shadow tree already resolved.
//
// That is a CPU renderer, and saying so plainly: every frame is drawn by Skia
// into a raster surface and copied once into a Direct2D bitmap. It is the same
// choice Win32SkiaContext.h already made for offscreen surfaces, for the same
// reason -- there is no GL or Vulkan context in this host to render into yet --
// and it is what makes the whole thing about two hundred lines instead of a
// WGL bring-up. A GPU path would replace `Win32SkiaCanvas`, and nothing else
// here: the seam it plugs into, `RnWin32Painter`, says nothing about pixels.
//
// What the walk buys, and why this is not a worse arrangement than a layer:
// the canvas is clipped by `overflow: hidden`, moved by a ScrollView's offset,
// faded by `opacity` and turned by `transform` because the view above it did
// those things before calling down -- none of which a sibling layer gets for
// free. It also appears in the offscreen snapshot, because the snapshot is the
// same walk against a WIC target.
//
// What it costs: a frame is a copy, and the window repaints whole. Neither is
// free and both are measured in the size of the canvas rather than the size of
// the app, which is the part that matters for a preview pane.

#pragma once

#include <react/renderer/core/ReactPrimitives.h>
#include <react/renderer/mounting/ShadowView.h>

#include <functional>

namespace basalt {

namespace win32 {
class RnWin32View;
}

// Gives `view` a Skia canvas if `shadowView` is a canvas view -- a
// SkiaPictureView on 2.x, a SkiaView on 3.x -- updating its size if it
// already has one. False when the component is something else, so the caller
// can treat this as a filter rather than asking twice.
//
// `requestRepaint` is how a picture pushed from JavaScript reaches the screen.
// Windows repaints on demand, so rendering a frame into the surface changes
// nothing until something invalidates the window -- and a canvas renders
// between mount transactions, which is exactly when nothing else will. It is
// the mounting manager's own `onDidMount` callback, for want of a second way
// to say "the screen is stale".
//
// Safe to call before `install` has run: without a manager there is nothing to
// register with, so it says so in the log and leaves the view alone rather
// than building a surface no picture could ever reach.
bool applySkiaCanvas(win32::RnWin32View *view,
                     const facebook::react::ShadowView &shadowView,
                     const std::function<void()> &requestRepaint);

// Unregisters the canvas for `tag`, if it had one. Called when the mounting
// walk forgets a tag, which is after the view itself has been deleted -- so
// this is only about the manager's registration and this file's own map. The
// surface goes with the view, which held the last reference to it.
void forgetSkiaCanvas(facebook::react::Tag tag);

} // namespace basalt
