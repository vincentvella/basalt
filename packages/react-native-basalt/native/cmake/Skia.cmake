# @shopify/react-native-skia, compiled from the app's own copy.
#
# The same arrangement as Worklets.cmake and for the same reason: the package
# ships its portable half as C++ over JSI, so the app's copy compiles at the
# app's version and nothing is vendored or pinned. What it does not ship for a
# desktop is the host half -- and here that half is unusually small, because the
# package's own division of labour happens to suit us.
#
# ## What is borrowed and what is replaced
#
# Of the fourteen Objective-C++ sources in `apple/`, seven touch React Native and
# seven do not, and the seven that do are precisely the ones a host would write
# anyway:
#
#   borrowed   MetalContext, MetalWindowContext, MetalLayerColorSpace,
#              SkiaCVPixelBufferUtils, RNSkAppleVideo, RNSkAppleView,
#              RNSkMetalCanvasProvider
#   replaced   RNSkiaModule and SkiaManager (the TurboModule),
#              RNSkApplePlatformContext (its *header* takes an RCTBridge and
#              builds a screenshot service from bridge.uiManager, so it cannot be
#              used as-is even though its .mm touches React in one place),
#              ViewScreenshotService, SkiaUIView, SkiaPictureView,
#              SkiaPictureViewManager
#
# MetalContext is the reason this is small. It is React-free and already offers
# MakeOffscreen, MakeImageFromBuffer, getDirectContext and -- for later --
# MakeWindow(CALayer *), which takes a layer rather than a React view. So a
# `<Canvas>` needs to hand over its own layer and nothing more.
#
# ## Three platforms, two of which build their own Skia
#
# Skia's binaries are per platform and the ones published are Apple's and
# Android's. macOS uses the published xcframeworks; Windows and Linux each
# build archives from source, laid out the way the package lays out Android's,
# by scripts/build_skia_windows.sh and scripts/build_skia_linux.sh.
#
# The three differ in more than a path, and each difference below says which
# platform it belongs to and why.

if(NOT DEFINED BASALT_SKIA)
  return()
endif()


get_filename_component(SKIA_DIR "${BASALT_SKIA}" ABSOLUTE)
set(SKIA_CPP_DIR ${SKIA_DIR}/cpp)
set(SKIA_APPLE_DIR ${SKIA_DIR}/apple)

# Where each platform's archives are.
#
# macos is the package's own layout, filled by its podspec or install-skia.
# windows and linux are ours, because nothing publishes binaries for either:
# both mirror the package's android layout -- libs/<platform>/<arch> -- and are
# what scripts/build_skia_windows.sh and scripts/build_skia_linux.sh write.
if(APPLE)
  set(SKIA_LIBS_DIR ${SKIA_DIR}/libs/macos)
elseif(WIN32)
  set(SKIA_LIBS_DIR ${SKIA_DIR}/libs/windows/x86_64)
else()
  set(SKIA_LIBS_DIR ${SKIA_DIR}/libs/linux/x86_64)
endif()

if(NOT EXISTS ${SKIA_CPP_DIR}/rnskia/RNSkManager.cpp)
  message(FATAL_ERROR
          "no @shopify/react-native-skia C++ at ${SKIA_CPP_DIR}.\n"
          "BASALT_SKIA should name an installed @shopify/react-native-skia, "
          "the one in the app's node_modules.")
endif()

# The binaries are not in the package's `files`: they are downloaded, by the
# podspec on a normal install or by the package's own install-skia script. So
# their absence is a thing that happens, and it has to say so rather than fail
# later in the link with a hundred undefined Skia symbols.
if(APPLE AND NOT EXISTS ${SKIA_LIBS_DIR}/libskia.xcframework)
  message(FATAL_ERROR
          "no Skia binaries at ${SKIA_LIBS_DIR}.\n"
          "They are downloaded rather than published, so a fresh checkout of an "
          "app may not have them yet. Run the app's pod install, or "
          "`yarn install-skia` inside ${SKIA_DIR}.")
endif()
if(WIN32 AND NOT EXISTS ${SKIA_LIBS_DIR}/skia.lib)
  message(FATAL_ERROR
          "no Skia binaries at ${SKIA_LIBS_DIR}.\n"
          "Nobody publishes any for Windows: the package ships Apple and Android "
          "archives and nothing else. Build them with scripts/build_skia_windows.sh, "
          "which puts them here.")
endif()
if(NOT APPLE AND NOT WIN32 AND NOT EXISTS ${SKIA_LIBS_DIR}/libskia.a)
  message(FATAL_ERROR
          "no Skia binaries at ${SKIA_LIBS_DIR}.\n"
          "Nobody publishes any for Linux either. Build them with "
          "scripts/build_skia_linux.sh, in WSL2 or on a Linux machine, and copy "
          "its libs/linux/x86_64 here.")
endif()

file(READ ${SKIA_DIR}/package.json SKIA_PACKAGE_JSON)
string(JSON SKIA_VERSION GET "${SKIA_PACKAGE_JSON}" version)

# Every prebuilt archive the package imports on Apple, each a universal slice.
# Named rather than globbed: a missing one is a link error in somebody else's
# symbols, and a list says which we expected.
#
# **Dependents before dependencies, so libskia is last.** A static archive is
# searched once, in the order given, and only for symbols already undefined. With
# libskia first the link failed on `skjson::DOM::DOM` referenced from
# libskottie.a -- skjson lives inside libskia, which the linker had already
# walked past. There is no --start-group on Apple's ld, so the order is the fix.
set(SKIA_LINK_LIBRARIES "")
if(APPLE)
  set(SKIA_ARCHIVES
          skottie svg skparagraph sksg skshaper
          skunicode_libgrapheme skunicode_core skia)
  foreach(archive ${SKIA_ARCHIVES})
    file(GLOB slice ${SKIA_LIBS_DIR}/lib${archive}.xcframework/macos-*/lib${archive}.a)
    if(NOT slice)
      message(FATAL_ERROR
              "no macOS slice for lib${archive} under ${SKIA_LIBS_DIR}")
    endif()
    list(GET slice 0 slice)
    list(APPEND SKIA_LINK_LIBRARIES ${slice})
  endforeach()
elseif(WIN32)
  # The same order and the same reason -- dependents first, skia last -- and
  # two differences that are the platform's, not a preference. The unicode
  # backend is the first, and it is immediately below.
  #
  # The bundled third-party archives are the second, and they are named. The
  # published Apple
  # xcframeworks carry theirs inside; a source build emits libpng, libjpeg,
  # libwebp, zlib, expat, harfbuzz, icu and wuffs as archives of their own, and
  # leaving them out is a link error in somebody else's symbols. After skia,
  # because skia is what refers to them.
  # skunicode_libgrapheme, which is also what the published Apple archives use,
  # and not skunicode_icu -- for a reason specific to this platform.
  #
  # ThirdParty.cmake links Windows' own icu.lib on purpose: the operating
  # system has ICU and vendoring a second copy is what that comment declines to
  # do. Skia's ICU-backed skunicode carries a static ICU inside it, and the two
  # export the same unsuffixed names, so a host that links both gets
  #
  #   lld-link: error: duplicate symbol: ures_getByKey
  #   >>> defined at skunicode_icu.lib(icu.uresbund.obj)
  #   >>> defined at icu.dll
  #
  # Neither side is wrong, and dropping either ICU loses something real. The
  # libgrapheme backend needs no ICU at all, which is presumably why Shopify
  # ships it on Apple. icu_bidi.lib is left out for the same reason: the
  # system's ICU supplies ubidi_*.
  set(SKIA_ARCHIVES
          skottie svg skparagraph sksg skshaper
          skunicode_libgrapheme skunicode_core skia
          harfbuzz libpng libjpeg libwebp libwebp_sse41 zlib expat wuffs)
  foreach(archive ${SKIA_ARCHIVES})
    set(lib ${SKIA_LIBS_DIR}/${archive}.lib)
    if(NOT EXISTS ${lib})
      # Not fatal for the bundled ones: which third-party archives a Skia build
      # emits depends on its GN args -- a build with skia_use_dng_sdk=false has
      # no dng_sdk.lib and needs none -- so a missing one is only wrong if
      # something later asks for its symbols, which the link will say plainly.
      if(archive MATCHES "^(skottie|svg|skparagraph|sksg|skshaper|skunicode_libgrapheme|skunicode_core|skia)$")
        message(FATAL_ERROR "no ${archive}.lib under ${SKIA_LIBS_DIR}")
      endif()
      continue()
    endif()
    list(APPEND SKIA_LINK_LIBRARIES ${lib})
  endforeach()
else()
  # Linux: the same nine the package's android/CMakeLists.txt imports, and no
  # more. Dependents first and skia last, as everywhere else here.
  #
  # **ICU, and why this is the opposite of the Windows choice above.** That
  # one cannot use skunicode_icu because basalt links the operating system's
  # ICU through icu.lib and the two export the same unsuffixed names. Here
  # nothing does: ThirdParty.cmake names icu only under `if(WIN32)`, because
  # on Linux Hermes is a shared libhermesvm.so that records its own ICU
  # dependency and resolves it at load time. So the collision has no second
  # party, and Linux gets the fuller backend -- ICU's line breaking and bidi
  # rather than libgrapheme's grapheme clusters alone.
  #
  # **And no third-party archives.** The Windows build emits libpng, libjpeg,
  # harfbuzz and the rest beside libskia and they have to be named; this one
  # folds them into libskia.a, so naming them would be naming files that do
  # not exist. Not a configuration difference anybody chose -- it is what the
  # two builds happen to emit -- so it is checked rather than assumed: every
  # name below is required, and a missing one is fatal.
  set(SKIA_ARCHIVES
          skottie svg skparagraph sksg skshaper
          skunicode_icu skunicode_core jsonreader skia)
  foreach(archive ${SKIA_ARCHIVES})
    set(lib ${SKIA_LIBS_DIR}/lib${archive}.a)
    if(NOT EXISTS ${lib})
      message(FATAL_ERROR "no lib${archive}.a under ${SKIA_LIBS_DIR}")
    endif()
    list(APPEND SKIA_LINK_LIBRARIES ${lib})
  endforeach()
endif()

file(GLOB_RECURSE SKIA_PORTABLE_SRC CONFIGURE_DEPENDS
        ${SKIA_CPP_DIR}/api/*.cpp
        ${SKIA_CPP_DIR}/jsi/*.cpp
        ${SKIA_CPP_DIR}/rnskia/*.cpp)

# Dawn is Skia's WebGPU backend, for the Graphite renderer. It needs Dawn's own
# headers, which the package downloads separately and only for a Graphite build,
# so on Ganesh these do not compile at all -- `webgpu/webgpu_cpp.h` not found.
# Ganesh over Metal is what the published Apple binaries are built for.
list(FILTER SKIA_PORTABLE_SRC EXCLUDE REGEX "/RNDawn")

# The package's vendored copy of Skia's own SkottieUtils stays compiled, and
# see the link options below for what that costs on Windows: it is the only
# definition of RNSkia::CustomPropertyManager, which the package's Skottie API
# needs, and it also redefines six skottie::PropertyObserver methods that
# skottie.lib defines.

# Skia's JSON reader, which Skottie parses Lottie files with. Compiled rather
# than imported: Android links a libjsonreader.a and no such archive is published
# for Apple, so on this platform it is source. The symptom of leaving it out is
# `skjson::DOM::DOM` undefined from libskottie -- a missing definition in
# somebody else's archive, which reads like a link-order problem and is not.
list(APPEND SKIA_PORTABLE_SRC
        ${SKIA_CPP_DIR}/skia/modules/jsonreader/SkJSONReader.cpp)

# The Apple sources that do not reach React. Named one by one on purpose: a glob
# would pull in SkiaUIView and RNSkiaModule the moment somebody upgraded the
# package, and the failure would be a wall of missing RCT headers rather than
# "this file is ours to write".
#
# Transitively, which is the part that caught me out. RNSkAppleView.mm mentions
# no RCT symbol and does not compile, because it includes
# RNSkApplePlatformContext.h, whose first line is <React/RCTBridge+Private.h>.
# Grepping the .mm files says seven are free; compiling them says six. So this
# list is the one that builds, not the one that reads as though it should.
#
# RNSkAppleView is therefore ours as well, and that is where `<Canvas>` will
# come in: RNSkMetalCanvasProvider below is the part that actually draws, and it
# is free, so what we owe is the view that owns a layer.
if(APPLE)
  set(SKIA_APPLE_SRC
          ${SKIA_APPLE_DIR}/MetalContext.mm
          ${SKIA_APPLE_DIR}/MetalLayerColorSpace.mm
          ${SKIA_APPLE_DIR}/MetalWindowContext.mm
          ${SKIA_APPLE_DIR}/RNSkAppleVideo.mm
          ${SKIA_APPLE_DIR}/RNSkMetalCanvasProvider.mm
          ${SKIA_APPLE_DIR}/SkiaCVPixelBufferUtils.mm)
else()
  # Nothing of the package's own on Windows or Linux. Its non-portable halves
  # are the Apple one above and an Android one that is JNI throughout -- the
  # platform context, the GL canvas provider and the window context all take
  # jobjects. So the whole host half on those two is basalt's: Win32Skia* in
  # the win32 package, GtkSkia* in the gtk one.
  set(SKIA_APPLE_SRC "")
endif()

# Objective-C++, because six of the sources below are .mm. Asked for here rather
# than assumed: this file is included from the *core* package, which is otherwise
# plain C++, and the AppKit package that enables OBJCXX for its own sources is a
# sibling that configures later. In this repository's own build something had
# already enabled it and this was invisible; configuring from an app -- which is
# a different top-level CMakeLists -- failed with `Missing variable is:
# CMAKE_OBJCXX_COMPILE_OBJECT`, which names the language and not the file.
#
# Idempotent, so the AppKit package asking again costs nothing.
#
# Apple only: there is not one .mm in the Windows set, and asking for a
# language no compiler here provides fails the configure outright.
if(APPLE)
  enable_language(OBJCXX)
endif()

add_library(skia_core OBJECT ${SKIA_PORTABLE_SRC} ${SKIA_APPLE_SRC})
# SYSTEM, and warnings off: somebody else's sources held to somebody else's
# warning set. This project's -Wall -Wextra -Werror is about its own code.
target_include_directories(skia_core SYSTEM PUBLIC
        ${SKIA_CPP_DIR}
        ${SKIA_CPP_DIR}/api
        ${SKIA_CPP_DIR}/jsi
        ${SKIA_CPP_DIR}/rnskia
        ${SKIA_CPP_DIR}/utils
        ${SKIA_CPP_DIR}/skia
        ${SKIA_CPP_DIR}/skia/include
        ${SKIA_CPP_DIR}/skia/modules
        ${SKIA_APPLE_DIR}
        ${RN_DIR}/ReactCommon
        ${RN_DIR}/ReactCommon/jsi
        ${RN_DIR}/ReactCommon/callinvoker
        ${RN_DIR}/ReactCommon/cxxreact
        ${RN_DIR}/ReactCommon/runtimeexecutor
        ${FOLLY_DIR})
if(APPLE)
  target_compile_options(skia_core PRIVATE -w -fobjc-arc)
  target_compile_definitions(skia_core PUBLIC
          SK_METAL=1
          SK_GANESH=1
          SKIA_VERSION=\"${SKIA_VERSION}\")
  target_link_libraries(skia_core ${SKIA_LINK_LIBRARIES}
          "-framework Metal"
          "-framework MetalKit"
          "-framework QuartzCore"
          "-framework CoreGraphics"
          "-framework CoreText"
          "-framework CoreVideo"
          "-framework CoreMedia"
          "-framework AVFoundation"
          "-framework Foundation")
elseif(WIN32)
  # /w rather than -w, and no ARC: the same "somebody else's warnings are not
  # this project's business" as above, spelled the way clang-cl takes it.
  target_compile_options(skia_core PRIVATE /w)
  # No SK_METAL. SK_GANESH alone is Skia's CPU-and-GL configuration, which is
  # what the Windows archives are built as -- and this host draws with the
  # raster half of it for now; see win32/Win32SkiaContext.h.
  target_compile_definitions(skia_core PUBLIC
          SK_GANESH=1
          SKIA_VERSION=\"${SKIA_VERSION}\")
  # What Skia's Windows backend reaches for. DirectWrite and Direct2D for
  # fonts, opengl32 for the GL backend the archives carry, and the COM and GDI
  # pieces its font and image code uses.
  target_link_libraries(skia_core ${SKIA_LINK_LIBRARIES}
          dwrite d2d1 opengl32 user32 gdi32 ole32 oleaut32 uuid usp10 windowscodecs)

  # /FORCE:MULTIPLE, for one overlap that cannot be resolved any other way.
  #
  # The package vendors a copy of Skia's SkottieUtils at
  # cpp/api/third_party/. That file is the only definition of
  # RNSkia::CustomPropertyManager, which its Skottie API needs, and its header
  # also defines six skottie::PropertyObserver methods out of line -- which
  # skottie.lib's SkottieProperty.obj defines too.
  #
  # Neither half can go. Dropping the vendored file loses
  # CustomPropertyManager: 42 undefined symbols. Dropping the archive member
  # loses the 32 others it carries -- PropertyHandle's instantiations,
  # TextPropertyValue's operators -- which skottie itself uses.
  #
  # Apple never sees this: ld pulls an archive member only when it still needs
  # one, and by then the vendored object has defined those six, so
  # SkottieProperty.obj is never pulled. lld-link pulls it for the other 32 and
  # then reports the six as duplicates. The two definitions are the same Skia
  # source, so either is right and first-wins is correct -- objects precede
  # libraries on the command line, which makes the winner the vendored one, the
  # same one Apple ends up with.
  #
  # The cost is that a genuine duplicate would also be demoted to a warning
  # here. LNK4006 in a build log is the thing to read if something linked and
  # then behaved as though it had two of something.
  target_link_options(skia_core INTERFACE /FORCE:MULTIPLE)
else()
  target_compile_options(skia_core PRIVATE -w)
  # SK_GANESH alone, as on Windows: the archives are built skia_use_gl=true
  # with Graphite and Dawn off, so this is Skia's CPU-and-GL configuration.
  target_compile_definitions(skia_core PUBLIC
          SK_GANESH=1
          SKIA_VERSION=\"${SKIA_VERSION}\")
  # What this Skia reaches outside its own archives.
  #
  # A short list, because the build bundles almost everything -- libpng,
  # libjpeg, libwebp, zlib, expat, harfbuzz and ICU are all inside libskia.a
  # and libskunicode_icu.a. What it deliberately does not bundle is freetype
  # and fontconfig: skia_use_system_freetype2=true and skia_use_fontconfig=true,
  # because a GTK host already links pango and cairo, which drag in freetype,
  # and two freetypes in one process is a crash rather than a tidiness
  # complaint. pkg-config rather than find_library so the include paths come
  # with them, and so a machine with them somewhere unusual still works.
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(SKIA_SYSTEM REQUIRED IMPORTED_TARGET fontconfig freetype2)
  # GL and EGL for the Ganesh backend the archives carry; dl and pthread
  # because a static Skia asks for both and nothing else on the line will.
  target_link_libraries(skia_core ${SKIA_LINK_LIBRARIES}
          PkgConfig::SKIA_SYSTEM
          GL EGL dl pthread)
  # No /FORCE:MULTIPLE counterpart, and none needed. The SkottieUtils overlap
  # the Windows branch describes is lld-link's alone: GNU ld, like Apple's,
  # pulls an archive member only while a symbol is still undefined, and by the
  # time it reaches skottie the vendored object has defined those six.
endif()

message(STATUS "Skia ${SKIA_VERSION} from ${SKIA_DIR}")
