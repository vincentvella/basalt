# @shopify/react-native-skia on the desktop.
#
# Skia itself is not this platform's to build. The npm package carries the C++
# that sits on top of it -- `cpp/{api,jsi,rnskia,utils}`, which is host-agnostic
# and speaks JSI -- and a second package per platform carries the prebuilt
# library: `react-native-skia-apple-macos` for this one, as universal static
# slices inside xcframeworks. Both are the app's own dependencies, so this
# capability compiles against what the app installed rather than vendoring a
# copy or building Skia from source.
#
# ## Why this is in this repository
#
# It is an adapter, and the thing it adapts is React Native's iOS half.
# `RNSkiaModule.mm` is a TurboModule over `RCT_EXPORT_BLOCKING_SYNCHRONOUS_METHOD`
# and `RNSkApplePlatformContext` takes an `RCTViewRegistry`; neither exists
# here. What does exist is a JSI runtime, a call invoker and a mounting
# manager, which is everything `RNSkManager` actually needs. So the port is a
# module and a platform context, and both belong where this project's CI can
# compile them -- the argument basalt-subprocess's CMakeLists makes about the
# Windows half of a capability.
#
# ## Absent is not an error
#
# A host build with no Skia installed contributes nothing and says so. That is
# the normal case for this repository's own CI, for Linux and for Windows:
# there is no prebuilt Skia for either of those yet, and no window context --
# Metal is what the Apple binaries ship. backlog/compatibility.md records what
# the other two would take.

set(BASALT_SKIA_DIR_DEFAULT "")
set(BASALT_SKIA_LIBS_DEFAULT "")
set(BASALT_SKIA_DAWN_DEFAULT "")

# Two package names and two binary packages, because the library moved house.
#
# `@shopify/react-native-skia` is the 2.x line and has not moved since
# 2026-10-01. `react-native-skia`, unscoped, is the same project under its
# maintainer's own name: it carries a `release-2.x` tag and its `latest` is
# 3.x, which replaced Ganesh with Skia Graphite and brings its binaries from
# `react-native-skia-graphite-apple-macos` rather than
# `react-native-skia-apple-macos`.
#
# Whichever the app installed is the one compiled against. That is the whole
# point of resolving this out of node_modules: a Skia version is the app's
# choice, and a platform that pinned one would be telling an app which
# graphics library to use.
foreach(root
    ${CMAKE_CURRENT_LIST_DIR}/../../..
    ${CMAKE_CURRENT_LIST_DIR}/../../../node_modules
    ${CMAKE_CURRENT_LIST_DIR}/../../node_modules)
  foreach(package react-native-skia @shopify/react-native-skia)
    if(EXISTS ${root}/${package}/cpp/skia/include/core/SkSurface.h)
      get_filename_component(BASALT_SKIA_DIR_DEFAULT ${root}/${package} ABSOLUTE)
    endif()
  endforeach()
  foreach(binaries react-native-skia-graphite-apple-macos react-native-skia-apple-macos)
    if(EXISTS ${root}/${binaries}/libs/libskia.xcframework)
      get_filename_component(BASALT_SKIA_LIBS_DEFAULT ${root}/${binaries} ABSOLUTE)
    endif()
  endforeach()
  # Dawn, which is 3.x's and only 3.x's: Graphite draws through WebGPU and the
  # package depends on `react-native-webgpu-dawn` for the implementation. A 2.x
  # install has no such dependency and needs none, which is why this is found
  # separately rather than assumed to sit beside the others.
  if(EXISTS ${root}/react-native-webgpu-dawn/include/webgpu/webgpu_cpp.h)
    get_filename_component(BASALT_SKIA_DAWN_DEFAULT
                           ${root}/react-native-webgpu-dawn ABSOLUTE)
  endif()
endforeach()

set(BASALT_SKIA_DIR "${BASALT_SKIA_DIR_DEFAULT}" CACHE PATH
    "@shopify/react-native-skia, which carries the C++ and Skia's headers")
set(BASALT_SKIA_LIBS "${BASALT_SKIA_LIBS_DEFAULT}" CACHE PATH
    "react-native-skia-apple-macos, which carries the prebuilt Skia slices")
set(BASALT_SKIA_DAWN "${BASALT_SKIA_DAWN_DEFAULT}" CACHE PATH
    "react-native-webgpu-dawn, which carries Dawn's headers and library (3.x only)")

if(NOT APPLE)
  message(STATUS "basalt-skia: skipped, the prebuilt Skia here is Apple's")
  return()
endif()
if(BASALT_SKIA_DIR STREQUAL "" OR BASALT_SKIA_LIBS STREQUAL "")
  message(STATUS
    "basalt-skia: skipped, no @shopify/react-native-skia and "
    "react-native-skia-apple-macos in the app's node_modules")
  return()
endif()

# Which one, said out loud: the two lines are a major version apart and the
# next thing to go wrong will be a C++ API that moved between them.
if(EXISTS ${BASALT_SKIA_DIR}/package.json)
  file(READ ${BASALT_SKIA_DIR}/package.json BASALT_SKIA_MANIFEST)
  string(JSON BASALT_SKIA_NAME ERROR_VARIABLE ignored GET ${BASALT_SKIA_MANIFEST} name)
  string(JSON BASALT_SKIA_VERSION ERROR_VARIABLE ignored GET ${BASALT_SKIA_MANIFEST} version)
  message(STATUS "basalt-skia: ${BASALT_SKIA_NAME} ${BASALT_SKIA_VERSION}")
endif()
message(STATUS "basalt-skia: Skia from ${BASALT_SKIA_DIR}")

# The static slices, which are universal: one file holds arm64 and x86_64, so
# there is no architecture to choose here and a cross build gets the right one
# from the linker.
#
# All of them rather than libskia alone: paragraph, shaper and unicode are
# separate archives and the api layer references them, so leaving one out is a
# link error rather than a smaller binary.
#
# **Dependents before dependencies, so libskia is last.** A static archive is
# searched once, in the order given, and only for symbols already undefined.
# With libskia first this failed on `skjson::Value::toString` referenced from
# libskottie -- skjson lives inside libskia, which the linker had already
# walked past. There is no --start-group on Apple's ld, so the order is the
# fix, and core's cmake/Skia.cmake records the same thing from the same
# symbol. It only surfaced once SkottieUtils.cpp was compiled: before that
# nothing referenced skjson and any order linked.
set(BASALT_SKIA_SLICE macos-arm64_x86_64)
foreach(library libskottie libsvg libskparagraph libsksg libskshaper
                libskunicode_libgrapheme libskunicode_core libskia)
  set(archive
      ${BASALT_SKIA_LIBS}/libs/${library}.xcframework/${BASALT_SKIA_SLICE}/${library}.a)
  if(NOT EXISTS ${archive})
    message(FATAL_ERROR "basalt-skia: ${archive} is missing; reinstall react-native-skia-apple-macos")
  endif()
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_LINK_LIBRARIES ${archive})
endforeach()

# What Skia's own headers are included as -- `include/core/SkSurface.h` --
# which is why the directory above them is on the path rather than the
# directory itself.
#
# As a *system* include, which is not tidiness: this project compiles with
# `-Wpedantic -Werror`, and `SkAssert.h` and `SkLog.h` paste `,` onto
# `__VA_ARGS__`, which is a GNU extension and an error here. Somebody else's
# headers are not this project's to keep warning-clean, which is the same
# reason React Native's are behind `-isystem`.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_SYSTEM_INCLUDES
  ${BASALT_SKIA_DIR}/cpp/skia)
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_INCLUDES
  ${CMAKE_CURRENT_LIST_DIR}
  # The canvas's own header, which the AppKit half and its tests include by
  # name. A directory of its own because the file in it is Objective-C++ and
  # everything beside this CMakeLists is portable.
  ${CMAKE_CURRENT_LIST_DIR}/appkit)

# The frameworks Skia's Apple backend calls into. Metal is for the GPU surface
# a `<Canvas>` will need; the rest are the CPU path, text and colour.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_LINK_LIBRARIES
  "-framework Metal"
  "-framework MetalKit"
  "-framework QuartzCore"
  "-framework CoreText"
  "-framework CoreGraphics"
  "-framework CoreServices")

# The platform context: what Skia asks a platform for, answered by this one.
#
# Into the *host's* sources rather than core's, and the reason is the linker
# rather than the language. None of this file is AppKit's -- it is portable
# C++ and will serve a Linux or Windows Skia unchanged -- but the Skia it
# calls into is `react-native-skia-graphite-apple-macos`, which only this
# host links. In core it goes into every host built from the same configure,
# and on a Mac that includes GTK: `basalt_gtk` failed to link with
# `sk_abort_no_print` undefined, referenced from BasaltSkPlatformContext.cpp
# inside libbasalt_core.a, because the GTK binary has no Skia archives and
# never will have Apple's.
#
# Found by building every target instead of one. The repository's own CI
# cannot see it -- it has no Skia installed, so this package contributes
# nothing there -- and the target built by hand while writing this was
# basalt_appkit_tests, which links the archives.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_SOURCES
  ${CMAKE_CURRENT_LIST_DIR}/BasaltSkPlatformContext.cpp)

# Skia's own C++, which this platform compiles rather than vendors: the
# `rnskia` layer the context derives from, and the JSI and api layers the
# manager installs. Also a system include, for the reason the headers are.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_SYSTEM_INCLUDES
  ${BASALT_SKIA_DIR}/cpp/rnskia
  ${BASALT_SKIA_DIR}/cpp)

# --- Skia's own C++, compiled ---------------------------------------------
#
# The headers above are enough to *use* Skia. `RNSkManager` -- the thing a
# module installs -- needs the package's own C++ compiled as well: eleven
# sources across `cpp/{api,jsi,rnskia}`, which is the whole portable half.
#
# In an object library of its own rather than appended to core's sources,
# because of the warning set. This project compiles with `-Wall -Wextra
# -Wpedantic -Werror` and these are somebody else's files; core's own
# cmake/Skia.cmake makes the same decision for the same reason, and the
# platform context stays in core's sources because that one *is* ours.
if(BASALT_SKIA_DAWN STREQUAL "")
  message(STATUS
    "basalt-skia: no react-native-webgpu-dawn, so Skia's own C++ is not "
    "compiled -- the headers and the library are still available")
else()
  enable_language(CXX)
  # One of the sources below is Objective-C++. Asked for here rather than
  # assumed, for the reason core's cmake/Skia.cmake gives: this runs before
  # the AppKit package that enables it for its own sources, and an app
  # configuring a different top-level CMakeLists failed with `Missing variable
  # is: CMAKE_OBJCXX_COMPILE_OBJECT`, which names the language and not the
  # file. Idempotent, so asking twice costs nothing.
  enable_language(OBJCXX)

  # Dawn is where 3.x draws, and it is not optional there: `api/JsiSkApi.h`
  # reaches `RNDawnContext.h`, so the api layer does not compile without
  # `webgpu/webgpu_cpp.h`. 2.x is Ganesh and has neither the dependency nor
  # these four sources, which is what the guard above is for.
  set(BASALT_SKIA_RNSKIA_SOURCES
      ${BASALT_SKIA_DIR}/cpp/jsi/JSICache.cpp
      ${BASALT_SKIA_DIR}/cpp/jsi/JsiPromises.cpp
      ${BASALT_SKIA_DIR}/cpp/jsi/Promise.cpp
      ${BASALT_SKIA_DIR}/cpp/jsi/ViewProperty.cpp
      ${BASALT_SKIA_DIR}/cpp/rnskia/RNDawnInterop.cpp
      ${BASALT_SKIA_DIR}/cpp/rnskia/RNSkGraphiteProducer.cpp
      ${BASALT_SKIA_DIR}/cpp/rnskia/RNSkManager.cpp
      ${BASALT_SKIA_DIR}/cpp/rnskia/RNSkWindowSurface.cpp
      ${BASALT_SKIA_DIR}/cpp/api/third_party/CSSColorParser.cpp
      ${BASALT_SKIA_DIR}/cpp/api/third_party/SkottieUtils.cpp
      ${BASALT_SKIA_DIR}/cpp/api/third_party/base64.cpp
      # Skia's JSON reader, which Skottie parses Lottie files with. Source
      # rather than an archive: Android links a libjsonreader.a and no such
      # archive is published for Apple. Leaving it out is `skjson::Value::
      # toString` undefined from libskottie -- a missing definition in
      # somebody else's archive, which reads like a link-order problem and is
      # a file nobody compiled.
      ${BASALT_SKIA_DIR}/cpp/skia/modules/jsonreader/SkJSONReader.cpp
      # The one Apple source 3.x still has that this platform needs, and it is
      # free: CoreGraphics, Foundation and QuartzCore, no React. `apple/` is
      # eleven files now where 2.x had fourteen, and the other ten are the
      # module, the view and the screenshot service -- all of them this
      # platform's own to write. `RNSkWindowSurface.cpp` calls into this one,
      # so leaving it out is `RNSkia::applyCAMetalLayerColorSpace` undefined.
      ${BASALT_SKIA_DIR}/apple/MetalLayerColorSpace.mm)
  foreach(source ${BASALT_SKIA_RNSKIA_SOURCES})
    if(NOT EXISTS ${source})
      message(FATAL_ERROR
        "basalt-skia: ${source} is missing. The portable half of "
        "react-native-skia is named file by file rather than globbed, so a "
        "version that moved one says which rather than failing in the link.")
    endif()
  endforeach()

  add_library(basalt_skia_rnskia OBJECT ${BASALT_SKIA_RNSKIA_SOURCES})
  set_target_properties(basalt_skia_rnskia PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON)
  # Somebody else's sources, so nothing of this project's warning set applies
  # and `-w` says so once rather than a suppression per diagnostic.
  target_compile_options(basalt_skia_rnskia PRIVATE -w)
  target_include_directories(basalt_skia_rnskia SYSTEM PRIVATE
    ${BASALT_SKIA_DIR}/cpp
    ${BASALT_SKIA_DIR}/cpp/api
    ${BASALT_SKIA_DIR}/cpp/jsi
    ${BASALT_SKIA_DIR}/cpp/rnskia
    ${BASALT_SKIA_DIR}/cpp/utils
    # Skia's own headers are included as `include/core/SkSurface.h` and its
    # private ones as `src/gpu/graphite/ContextOptionsPriv.h`, so the
    # directory *above* both is what goes on the path.
    ${BASALT_SKIA_DIR}/cpp/skia
    ${BASALT_SKIA_DIR}/cpp/skia/modules
    # For MetalLayerColorSpaceUtils.h, which the .mm beside it includes by
    # name.
    ${BASALT_SKIA_DIR}/apple
    ${BASALT_SKIA_DAWN}/include
    ${RN_DIR}/ReactCommon
    ${RN_DIR}/ReactCommon/jsi
    ${RN_DIR}/ReactCommon/callinvoker
    ${RN_DIR}/ReactCommon/cxxreact
    ${RN_DIR}/ReactCommon/runtimeexecutor)

  # The objects, and Dawn under them. A universal slice like Skia's, so there
  # is no architecture to pick.
  set(BASALT_SKIA_DAWN_ARCHIVE
      ${BASALT_SKIA_DAWN}/libs/apple/libwebgpu_dawn.xcframework/${BASALT_SKIA_SLICE}/libwebgpu_dawn.a)
  if(NOT EXISTS ${BASALT_SKIA_DAWN_ARCHIVE})
    message(FATAL_ERROR
      "basalt-skia: ${BASALT_SKIA_DAWN_ARCHIVE} is missing; reinstall "
      "react-native-webgpu-dawn")
  endif()
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_LINK_LIBRARIES
    basalt_skia_rnskia
    ${BASALT_SKIA_DAWN_ARCHIVE}
    # Dawn's Metal backend, and IOSurface for the textures it shares.
    "-framework IOSurface"
    "-framework IOKit")
  # The platform context includes `RNDawnContext.h` once Dawn is here, so
  # core's half of the include path grows with it.
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_SYSTEM_INCLUDES
    ${BASALT_SKIA_DIR}/cpp/api
    ${BASALT_SKIA_DIR}/cpp/utils
    ${BASALT_SKIA_DIR}/cpp/skia/modules
    ${BASALT_SKIA_DAWN}/include)
  # The module, which needs RNSkManager and therefore the object library
  # above: this is the half of the package that only exists where Skia's own
  # C++ was compiled. On a 2.x install the AppKit package's module answers to
  # the same name instead; see BasaltSkiaModule.h for why they never collide.
  #
  # Into the host's sources for the reason the platform context is, and *not*
  # through BASALT_PACKAGE_TURBO_MODULES, which would be worse than the same
  # bug: that property has core generate a declaration and a call for the
  # factory, so a GTK binary built from this configure would reference
  # `makeSkiaTurboModule` and find no definition. The AppKit host registers it
  # by name instead, beside the 2.x module and under the same kind of guard --
  # which is how 2.x has always done it.
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/BasaltSkiaModule.cpp)
  # The canvas, which is the only AppKit-specific file this package has: a
  # CAMetalLayer and its lifecycle. Contributed to the host's sources rather
  # than core's, because it includes RnAppKitView.h and compiles inside the
  # AppKit target, and only here because the prebuilt Dawn is Apple's.
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_SOURCES
    ${CMAKE_CURRENT_LIST_DIR}/appkit/BasaltSkiaCanvasAppKit.mm)
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_TESTS
    ${CMAKE_CURRENT_LIST_DIR}/tests/test_skia_module.cpp
    ${CMAKE_CURRENT_LIST_DIR}/tests/test_skia_window_surface.mm
    ${CMAKE_CURRENT_LIST_DIR}/tests/test_skia_canvas.mm)

  # Said out loud rather than probed for. `__has_include(<webgpu/webgpu_cpp.h>)`
  # answers the same question and three files were asking it, which is three
  # places to be wrong about what the include path happens to carry. One
  # definition, from the configure that made the decision.
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_DEFINITIONS
    BASALT_HAS_SKIA_GRAPHITE=1)

  message(STATUS "basalt-skia: Dawn from ${BASALT_SKIA_DAWN}")
endif()

# Proof that the above is true, in this project's own AppKit suite rather than
# in an app: Skia's headers compile here and its prebuilt library links and
# draws. Everything else this capability will do stands on that.
# BASALT_PACKAGE_APPKIT_TESTS rather than BASALT_PACKAGE_TESTS, which every
# host's suite takes: two of these are Objective-C++ and all of them link
# Skia, so the GTK suite compiling them failed on an `AppKitSkiaPeer.h` it has
# no path to -- and would have failed on the archives even if it had.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_TESTS
  ${CMAKE_CURRENT_LIST_DIR}/tests/test_skia.cpp
  ${CMAKE_CURRENT_LIST_DIR}/tests/test_skia_platform_context.cpp)
