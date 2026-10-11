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
endforeach()

set(BASALT_SKIA_DIR "${BASALT_SKIA_DIR_DEFAULT}" CACHE PATH
    "@shopify/react-native-skia, which carries the C++ and Skia's headers")
set(BASALT_SKIA_LIBS "${BASALT_SKIA_LIBS_DEFAULT}" CACHE PATH
    "react-native-skia-apple-macos, which carries the prebuilt Skia slices")

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
set(BASALT_SKIA_SLICE macos-arm64_x86_64)
foreach(library libskia libsvg libskshaper libskparagraph libskottie libsksg
                libskunicode_core libskunicode_libgrapheme)
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
  ${CMAKE_CURRENT_LIST_DIR})

# The frameworks Skia's Apple backend calls into. Metal is for the GPU surface
# a `<Canvas>` will need; the rest are the CPU path, text and colour.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_LINK_LIBRARIES
  "-framework Metal"
  "-framework MetalKit"
  "-framework QuartzCore"
  "-framework CoreText"
  "-framework CoreGraphics"
  "-framework CoreServices")

# Proof that the above is true, in this project's own AppKit suite rather than
# in an app: Skia's headers compile here and its prebuilt library links and
# draws. Everything else this capability will do stands on that.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_TESTS
  ${CMAKE_CURRENT_LIST_DIR}/tests/test_skia.cpp)
