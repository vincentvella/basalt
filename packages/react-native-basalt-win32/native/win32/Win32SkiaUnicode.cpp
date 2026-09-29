// `SkUnicodes::ICU::Make`, answered with libgrapheme.
//
// One symbol, and it exists because of a disagreement between two reasonable
// choices rather than a mistake by either.
//
// @shopify/react-native-skia picks its unicode backend by platform, in
// cpp/api/JsiSkParagraphBuilder.h:
//
//     #ifdef __APPLE__
//         unicode = SkUnicodes::Libgrapheme::Make();
//     #else
//         unicode = SkUnicodes::ICU::Make();
//     #endif
//
// Windows is not __APPLE__, so it asks for ICU. This host cannot give it the
// ICU-backed skunicode: basalt links the operating system's own ICU -- see
// cmake/ThirdParty.cmake, which declines to vendor a second copy -- and Skia's
// ICU-backed skunicode carries a static one, so the two collide on every
// unsuffixed ICU symbol. The archives here are therefore built with
// libgrapheme, exactly as the package's own published Apple archives are.
//
// Which leaves the package calling a factory nobody defines. Defining it here
// is the smallest honest answer: the same backend Apple gets, under the name
// this platform's branch of that #ifdef asks for. The alternative is patching
// the package in every app that uses it, which is a worse version of this.
//
// It is not a stub. `SkUnicodes::Libgrapheme::Make()` is the real
// implementation, shipped in skunicode_libgrapheme.lib, and paragraph layout
// on this host is the same code Apple runs. What differs from an ICU build is
// what libgrapheme does not do -- ICU's full line-breaking and bidi tables --
// which is a difference Apple already lives with.

#include "modules/skunicode/include/SkUnicode_icu.h"
#include "modules/skunicode/include/SkUnicode_libgrapheme.h"

namespace SkUnicodes::ICU {

sk_sp<SkUnicode> Make() {
  return SkUnicodes::Libgrapheme::Make();
}

} // namespace SkUnicodes::ICU
