// What @shopify/react-native-skia needs from the GTK host.
//
// `RNSkPlatformContext` is the package's one seam: it hands Skia a runtime, a
// call invoker and this. The package ships an Apple implementation and an
// Android one, and neither can stand in here -- the Apple header takes an
// `RCTBridge *`, and Android's platform context and GL canvas provider are JNI
// all the way down. So this is basalt's, as appkit/AppKitSkiaContext.h and
// win32/Win32SkiaContext.h are.
//
// ## Raster, for now
//
// AppKit's delegates to the package's `MetalContext` and has a GPU context to
// hand out. This one has none yet: `getDirectContext` answers null and
// `makeOffscreenSurface` returns a CPU surface.
//
// That is a deliberate first stage rather than an oversight, and it is the
// same stage Windows is at. It is enough for the imperative API -- `Skia.Path`,
// image decoding, typefaces -- and enough for `<Canvas>`, which renders into
// one of these surfaces and reaches the screen as a texture; see
// gtk/GtkSkiaPeer.h.
//
// A GPU stage has further to go here than it looks, and further than on the
// other two. The archives are built `skia_use_gl=true skia_use_egl=true`, so
// the backend is present, and GTK4 has GtkGLArea. What is missing is the piece
// in between: Ganesh wants a `GrDirectContext` made from a current GL context,
// and GtkGLArea only makes one current inside its own render signal -- so the
// context this class hands out would have to be created and used on GTK's
// terms rather than on Skia's. That is a real design question, not a port, and
// it is why this stage is raster.
//
// ## Fonts
//
// Fontconfig, through Skia's own `SkFontMgr_New_FontConfig`, which is what the
// Linux archives are built for -- `skia_use_fontconfig=true` and
// `skia_use_system_freetype2=true` in scripts/build_skia_linux.sh. System
// freetype rather than a bundled one on purpose: a GTK host already links pango
// and cairo, which drag in freetype, and two freetypes in one process is a
// crash rather than an untidiness. The same seam the other two hosts answer
// with CoreText and DirectWrite.
//
// ## No unicode adapter here
//
// win32/Win32SkiaUnicode.cpp exists because that host links the libgrapheme
// backend while the package's JsiSkParagraphBuilder asks for
// `SkUnicodes::ICU::Make` on anything that is not Apple. This host links
// skunicode_icu, which defines that name itself -- see the note in
// cmake/Skia.cmake for why Linux can have the ICU backend and Windows cannot.

#pragma once

#include "RNSkPlatformContext.h"

#include <memory>

namespace basalt {

// The platform context this host gives Skia. One per install; see
// gtk/GtkSkiaModule.h.
std::shared_ptr<RNSkia::RNSkPlatformContext>
makeSkiaPlatformContext(std::shared_ptr<facebook::react::CallInvoker> callInvoker);

} // namespace basalt
