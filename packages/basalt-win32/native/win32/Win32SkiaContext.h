// What @shopify/react-native-skia needs from this host.
//
// `RNSkPlatformContext` is the package's one seam: it hands Skia a runtime, a
// call invoker and this. The package ships an Apple implementation and an
// Android one, and neither can stand in here -- the Apple header takes an
// `RCTBridge *`, and Android's platform context and GL canvas provider are JNI
// all the way down. So this is basalt's, as appkit/AppKitSkiaContext.h is.
//
// ## Raster, for now
//
// AppKit's delegates to the package's `MetalContext` and has a GPU context to
// hand out. This one has none: `getDirectContext` answers null and
// `makeOffscreenSurface` returns a CPU surface.
//
// That is a deliberate first stage rather than an oversight, and it is enough
// for the imperative API, which is what an app reaching for `Skia.Path`, image
// decoding and typefaces uses. Reading the package's own call sites at 2.11.1:
// `encodeToBytes` asks for the context only when an image `isTextureBacked()`,
// which a decoded one is not; `readPixels` passes it straight to Skia, where
// null means the CPU path; and `makeNonTextureImage` throws "No GPU context
// available", which is the honest answer for an image that is already raster.
//
// `<Canvas>` is raster too, and it works: win32/Win32SkiaPeer.h renders each
// one into a surface from `makeOffscreenSurface` and hands the pixels to the
// Direct2D walk, because a view here is not a window and there is no layer to
// give the package instead. What that costs is a copy per frame and a CPU
// rasteriser; what it buys is that a canvas is clipped, scrolled and faded by
// the views above it, and appears in the offscreen snapshot, without a line of
// code for any of it.
//
// A GPU stage would be WGL or ANGLE over D3D behind `getDirectContext`, and
// would replace the surface in that peer and nothing else. The archives are
// already built with `skia_use_gl=true` against the day it is wanted.
//
// ## Fonts
//
// DirectWrite, through Skia's own `SkFontMgr_New_DirectWrite`, which is what
// the Windows archives are built with -- freetype and fontconfig are off in the
// GN args, where the Linux build has them on. The same seam, answered by
// whatever the platform's real font system is.

#pragma once

#include "RNSkPlatformContext.h"

#include <memory>

namespace basalt {

std::shared_ptr<RNSkia::RNSkPlatformContext>
makeSkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker);

} // namespace basalt
