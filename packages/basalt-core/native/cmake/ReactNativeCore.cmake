# Adds the React Native C++ targets a host needs in order to run Fabric's
# mounting layer.
#
# This is the transitive closure of react_renderer_mounting, computed from the
# target_link_libraries in RN's own CMakeLists. Notably it contains no Hermes:
# constructing and applying mutations does not require a JS runtime. Adding one
# (react/runtime + react/runtime/hermes) is a separate, later step.

set(REACT_COMMON_DIR ${RN_DIR}/ReactCommon)
# jsinspector-modern's CMakeLists include SoMerging-utils.cmake from here. It
# is an Android .so-merging helper that no-ops off-Android, but the include
# path must still resolve.
set(REACT_ANDROID_DIR ${RN_DIR}/ReactAndroid)

# Several RN CMakeLists call react_native_android_selector() without including
# the file that defines it, relying on a parent scope having done so.
include(${REACT_COMMON_DIR}/cmake-utils/internal/react-native-platform-selector.cmake)
if(DEFINED RN_CXX_PLATFORM_OVERRIDE)
  set(REACT_CXX_PLATFORM_DIR ${RN_CXX_PLATFORM_OVERRIDE})
else()
  set(REACT_CXX_PLATFORM_DIR ${RN_DIR}/ReactCxxPlatform)
endif()

# The version being built against, for the messages below and for anyone
# wondering which React Native a given build came from. `main` calls itself
# 1000.0.0, which is React Native's placeholder for "not a release" and reads
# as exactly that in the output. Parsed with a regex rather than string(JSON)
# to keep the CMake floor where it is.
file(READ ${RN_DIR}/package.json RN_PACKAGE_JSON)
string(REGEX MATCH "\"version\"[ \t]*:[ \t]*\"([^\"]+)\"" _ ${RN_PACKAGE_JSON})
set(RN_VERSION ${CMAKE_MATCH_1})
message(STATUS "React Native ${RN_VERSION} (${RN_DIR})")

# Reported to JavaScript by core/PlatformConstantsModule.cpp, which exists
# because ReactCxxPlatform hardcodes 1000.0.0 and so can never match a release.
add_compile_definitions(BASALT_REACT_NATIVE_VERSION="${RN_VERSION}")

# The minor, as a number, for the handful of places this platform's own sources
# have to differ between supported React Natives. `main` calls itself 1000.0.0,
# which sorts above every release, which is what you want.
string(REGEX MATCH "^([0-9]+)\\.([0-9]+)" _ ${RN_VERSION})
math(EXPR RN_MINOR "${CMAKE_MATCH_1} * 1000 + ${CMAKE_MATCH_2}")
add_compile_definitions(BASALT_RN_MINOR=${RN_MINOR})
message(STATUS "React Native minor code ${RN_MINOR}")

# Codegen output belongs to exactly one React Native version: the generated
# spec headers name every feature flag that version has. Building against a
# different one fails deep inside React Native's own sources, on flags that
# look like they should exist, and says nothing about the real cause. Ask here
# instead, where the answer is a single command.
# Nothing here without bootstrap. Checked before the version stamp because an
# absent directory and a stale one need the same command, and because CMake's
# own complaint further down names a missing CMakeLists.txt, which sends you
# looking for a file this project does not ask you to write.
if(NOT EXISTS ${BASALT_THIRD_PARTY}/codegen/CMakeLists.txt)
  get_filename_component(RN_MONOREPO_DIR "${RN_DIR}/../.." ABSOLUTE)
  message(FATAL_ERROR
          "no codegen output at ${BASALT_THIRD_PARTY}/codegen.\n"
          "Generate it with:\n"
          "  scripts/bootstrap.sh ${RN_MONOREPO_DIR}\n")
endif()

set(RN_CODEGEN_STAMP ${BASALT_THIRD_PARTY}/codegen/.react-native-version)
if(EXISTS ${RN_CODEGEN_STAMP})
  file(READ ${RN_CODEGEN_STAMP} RN_CODEGEN_VERSION)
  string(STRIP "${RN_CODEGEN_VERSION}" RN_CODEGEN_VERSION)
  if(NOT RN_CODEGEN_VERSION STREQUAL RN_VERSION)
    get_filename_component(RN_MONOREPO_DIR "${RN_DIR}/../.." ABSOLUTE)
    message(FATAL_ERROR
            "third_party/codegen was generated from React Native ${RN_CODEGEN_VERSION}, "
            "but this build is against ${RN_VERSION}.\n"
            "Regenerate it with:\n"
            "  scripts/bootstrap.sh ${RN_MONOREPO_DIR}\n")
  endif()
endif()

# Skips a directory this React Native does not have, rather than failing.
#
# The list below is written against `main`, and a release is `main` minus
# whatever landed after it -- 0.87.1, for instance, has no ResizeObserver. A
# hard failure there would mean one build file per supported version, so
# absence is reported and carried on from instead. What is skipped is printed,
# because silently building less than intended is the worse failure.
function(add_react_common_subdir relative_path)
  if(NOT EXISTS ${REACT_COMMON_DIR}/${relative_path}/CMakeLists.txt)
    set(RN_SKIPPED_SUBDIRS ${RN_SKIPPED_SUBDIRS} ${relative_path} PARENT_SCOPE)
    return()
  endif()
  add_subdirectory(${REACT_COMMON_DIR}/${relative_path} ReactCommon/${relative_path})
endfunction()

set(RN_CORE_SUBDIRS
        callinvoker
        cxxreact
        devtoolsruntimesettings
        hermes/executor
        hermes/inspector-modern
        jserrorhandler
        jsi
        jsiexecutor
        jsinspector-modern
        jsinspector-modern/cdp
        jsinspector-modern/network
        jsinspector-modern/tracing
        jsitooling
        logger
        oscompat
        react/bridging
        react/cxxstableapi
        react/debug
        react/featureflags
        react/nativemodule/core
        # Deliberately not react/nativemodule/cputime. React Native ships that
        # module's C++ in the npm package but not its codegen spec, which lives
        # under src/private/testing/fantom and is excluded, so it cannot be
        # built from an installed React Native at all. Nothing links it -- it is
        # a Fantom testing module -- so this platform simply does not build it.
        react/nativemodule/defaults
        react/nativemodule/devtoolsruntimesettings
        react/nativemodule/dom
        react/nativemodule/featureflags
        react/nativemodule/idlecallbacks
        react/nativemodule/intersectionobserver
        react/nativemodule/microtasks
        react/nativemodule/mutationobserver
        react/nativemodule/resizeobserver
        react/nativemodule/viewtransition
        react/nativemodule/webperformance
        react/performance/cdpmetrics
        react/performance/timeline
        react/renderer/animated
        react/renderer/animationbackend
        react/renderer/attributedstring
        react/renderer/bridging
        react/renderer/componentregistry
        react/renderer/componentregistry/native
        react/renderer/components/image
        react/renderer/components/legacyviewmanagerinterop
        react/renderer/components/modal
        react/renderer/components/root
        react/renderer/components/scrollview
        react/renderer/components/text
        react/renderer/components/view
        react/renderer/consistency
        react/renderer/core
        react/renderer/css
        react/renderer/debug
        react/renderer/dom
        react/renderer/graphics
        react/renderer/imagemanager
        react/renderer/leakchecker
        react/renderer/mapbuffer
        react/renderer/mounting
        react/renderer/observers/events
        react/renderer/observers/intersection
        react/renderer/observers/mutation
        react/renderer/observers/resize
        react/renderer/runtimescheduler
        react/renderer/scheduler
        react/renderer/telemetry
        react/renderer/textlayoutmanager
        react/renderer/uimanager
        react/renderer/uimanager/consistency
        react/renderer/viewtransition
        react/runtime
        react/runtime/hermes
        react/timing
        react/utils
        reactperflogger
        runtimeexecutor
        yoga)

# Seams with per-platform variants: a host must choose one of each, globally,
# before any RN target is added. Individual RN CMakeLists only add the variant
# dirs they own, so a target like componentregistry -- which includes ViewProps
# transitively -- otherwise cannot find HostPlatformViewProps.h.
include_directories(
        ${REACT_COMMON_DIR}/react/renderer/graphics/platform/cxx
        ${REACT_COMMON_DIR}/react/renderer/components/view/platform/cxx
        ${REACT_COMMON_DIR}/react/renderer/textlayoutmanager/platform/cxx
        ${REACT_COMMON_DIR}/react/renderer/imagemanager/platform/cxx
        ${REACT_COMMON_DIR}/react/utils/platform/cxx
        ${REACT_COMMON_DIR}/runtimeexecutor/platform/cxx)

set(RN_SKIPPED_SUBDIRS)
foreach(subdir ${RN_CORE_SUBDIRS})
  add_react_common_subdir(${subdir})
endforeach()
if(RN_SKIPPED_SUBDIRS)
  message(STATUS "React Native ${RN_VERSION} has no: ${RN_SKIPPED_SUBDIRS}")
endif()

# yoga/CMakeLists.txt descends into yoga/yoga, which names the target
# `yogacore`. RN's other CMakeLists link against `yoga`. Fantom bridges the
# two the same way.
add_library(yoga ALIAS yogacore)

# ---------------------------------------------------------------------------
# ReactCxxPlatform — the generic C++ platform: ReactHost, the scheduler
# delegate, http, io, logging, threading, devsupport, coremodules and the
# TurboModule host. This is the layer that makes a non-Apple, non-Android host
# possible at all.
# ---------------------------------------------------------------------------

# React Native's generated codegen artifacts. 24 targets depend on
# react_codegen_rncore, including ReactCxxPlatform's react/runtime -- where
# ReactHost lives -- so this is not optional for a host that runs JS.
add_subdirectory(${CODEGEN_DIR} codegen)

function(add_react_cxx_platform_subdir relative_path)
  add_subdirectory(${REACT_CXX_PLATFORM_DIR}/${relative_path} ReactCxxPlatform/${relative_path})
endfunction()

set(RN_CXX_PLATFORM_SUBDIRS
        react/coremodules
        react/devsupport
        react/http
        react/io
        react/logging
        react/nativemodule
        react/profiling
        react/renderer/scheduler
        react/renderer/uimanager
        react/runtime
        react/threading
        react/utils)

foreach(subdir ${RN_CXX_PLATFORM_SUBDIRS})
  add_react_cxx_platform_subdir(${subdir})
endforeach()

set(RN_CXX_PLATFORM_TARGETS
        react_cxx_platform_react_coremodules
        react_cxx_platform_react_devsupport
        react_cxx_platform_react_http
        react_cxx_platform_react_io
        react_cxx_platform_react_logging
        react_cxx_platform_react_nativemodule
        react_cxx_platform_react_profiling
        react_cxx_platform_react_renderer_scheduler
        react_cxx_platform_react_renderer_uimanager
        react_cxx_platform_react_runtime
        react_cxx_platform_react_threading
        react_cxx_platform_react_utils)

# Every RN target above is an OBJECT library; collect them into one thing that
# is convenient to link.
set(RN_CORE_OBJECT_TARGETS
        bridgeless
        bridgelesshermes
        callinvoker
        devtoolsruntimesettings
        hermes_executor_common
        hermes_inspector_modern
        jserrorhandler
        jsi
        jsinspector
        jsinspector_cdp
        jsinspector_network
        jsinspector_tracing
        jsireact
        jsitooling
        logger
        oscompat
        react_bridging
        react_cxxreact
        react_cxxstableapi
        react_debug
        react_featureflags
        react_nativemodule_core
        react_nativemodule_defaults
        react_nativemodule_devtoolsruntimesettings
        react_nativemodule_dom
        react_nativemodule_featureflags
        react_nativemodule_idlecallbacks
        react_nativemodule_intersectionobserver
        react_nativemodule_microtasks
        react_nativemodule_mutationobserver
        react_nativemodule_resizeobserver
        react_nativemodule_viewtransition
        react_nativemodule_webperformance
        react_performance_cdpmetrics
        react_performance_timeline
        react_renderer_animated
        react_renderer_animationbackend
        react_renderer_attributedstring
        react_renderer_bridging
        react_renderer_componentregistry
        react_renderer_consistency
        react_renderer_core
        react_renderer_css
        react_renderer_debug
        react_renderer_dom
        react_renderer_graphics
        react_renderer_imagemanager
        react_renderer_leakchecker
        react_renderer_mapbuffer
        react_renderer_mounting
        react_renderer_observers_events
        react_renderer_observers_intersection
        react_renderer_observers_mutation
        react_renderer_observers_resize
        react_renderer_runtimescheduler
        react_renderer_scheduler
        react_renderer_telemetry
        react_renderer_textlayoutmanager
        react_renderer_uimanager
        react_renderer_uimanager_consistency
        react_renderer_viewtransition
        react_timing
        react_utils
        reactperflogger
        rrc_image
        rrc_legacyviewmanagerinterop
        rrc_modal
        rrc_native
        rrc_root
        rrc_scrollview
        rrc_text
        rrc_view
        runtimeexecutor)

# Must come after the set() above, which would otherwise overwrite it.
list(APPEND RN_CORE_OBJECT_TARGETS ${RN_CXX_PLATFORM_TARGETS})

# Same reasoning as add_react_common_subdir: a target belonging to a directory
# this version does not ship is not an error. Filtered here rather than at each
# use, so everything downstream can assume the list is real.
set(RN_PRESENT_TARGETS)
set(RN_ABSENT_TARGETS)
foreach(target ${RN_CORE_OBJECT_TARGETS})
  if(TARGET ${target})
    list(APPEND RN_PRESENT_TARGETS ${target})
  else()
    list(APPEND RN_ABSENT_TARGETS ${target})
  endif()
endforeach()
if(RN_ABSENT_TARGETS)
  message(STATUS "React Native ${RN_VERSION} defines no: ${RN_ABSENT_TARGETS}")
endif()
set(RN_CORE_OBJECT_TARGETS ${RN_PRESENT_TARGETS})

# Upstream portability bug: ReactCommon/jsinspector-modern/network/HttpUtils.h
# uses uint16_t without including <cstdint>. It compiles on Meta's toolchains
# only via transitive includes. Force-including <cstdint> fixes it without
# patching the React Native checkout, and is harmless everywhere else.
# React Native's own targets build with -Werror, and on Linux clang uses
# libstdc++, whose std::bind instantiates the deprecated std::result_of
# internally (ReactCommon/cxxreact/CxxModule.h:83 is the trigger). libc++ --
# what Apple platforms and the Android NDK use -- does not, which is why this
# only appears here. Nothing in React Native's code is actually wrong, so the
# warning is suppressed rather than worked around.
#
# libc++ would avoid it, but every other dependency on a Linux distro
# (glog, fmt, boost_regex, double-conversion) is built against libstdc++, and
# mixing the two is an ABI hazard for anything passing a std::string.
set(RN_EXTRA_FLAGS ${BASALT_FORCE_INCLUDE_CSTDINT_FLAG})
if(NOT APPLE AND NOT WIN32)
  list(APPEND RN_EXTRA_FLAGS ${BASALT_NO_DEPRECATED_FLAG})
endif()

# --- React Native's own flags, on a compiler they were not written for -------
#
# ReactCommon/cmake-utils/react-native-flags.cmake puts this on every target it
# touches:
#
#     -Wall -Werror -fexceptions -frtti -std=c++20
#
# and immediately below it says `TODO T228344694 improve this so that it works
# for all platforms`, which is this problem, acknowledged upstream and not yet
# fixed. Three of those five are clang command-line spellings that MSVC's front
# end does not have, and `-std=c++20` is actively harmful: clang-cl discards it
# with a warning, and cl would take it as a filename.
#
# Patching the checkout is not an option -- nothing here modifies React Native,
# which is the property that lets this platform track it. Removing the flags
# from each target afterwards was tried and does not work either: React Native
# applies them PUBLIC, so every copy also lives in the INTERFACE_COMPILE_OPTIONS
# of everything a target links, and they come back on the command line *after*
# the ones taken off. Chasing them through the link graph would be a fight with
# the build system on every upstream bump.
#
# So the flags are left alone and clang-cl is told not to treat its own
# "I ignored this" as an error. That is the honest reading of the situation:
# clang-cl is right to ignore them, and nothing is lost by it. `-std=c++20` is
# already supplied as `/std:c++20` from CXX_STANDARD, and exceptions and RTTI
# are added below in MSVC's spelling -- so all three are in force, just spelled
# the other way. `-Wall -Werror` are understood by clang-cl and kept, because
# building React Native's C++ warning-free is what catches an upstream change
# early.
#
# This is also why the core build needs clang-cl rather than cl. cl does not
# understand `-Werror` or clang's `-Wall` at all, and its own `/Wall` is a
# different and far noisier set; the Windows *view layer* builds fine with
# either, and CI uses cl for it.
if(WIN32)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(WARNING
      "The Windows core build expects clang-cl. React Native's own CMake sets "
      "clang-style flags (see ReactCommon/cmake-utils/react-native-flags.cmake "
      "and its TODO T228344694), which cl does not understand. Configure with "
      "-DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl.")
  endif()
  # And `-Wall` is worse than ignored -- it is misread. To clang-cl the token
  # `-Wall` is MSVC's `/Wall`, which it maps to clang's `-Weverything`: not
  # "the usual warnings" but *every* warning clang has, including
  # -Wc++98-compat, which fires on every nested namespace and defaulted
  # constructor in a C++20 codebase. React Native's own header for the event
  # beat produces about forty errors from that alone.
  #
  # `-Wno-everything` undoes the misreading. What it does not do is restore
  # what React Native meant, so React Native's C++ is compiled here with its
  # warnings off -- which is a real loss, because warning-free upstream code is
  # what catches an API change early, and it is why the Linux build stays the
  # one that guards that. Recorded in docs/BACKLOG.md rather than papered over.
  #
  # This project's *own* sources are unaffected: they are held to /W4
  # /permissive- by the platform package, and none of this reaches them.
  # Applied at the bottom of this file, to every target in the build graph
  # rather than to a list -- see the walk there for why.
  set(RN_WINDOWS_FLAGS
    -Wno-unknown-argument
    -Wno-everything
    # M_PI is a POSIX extension, not standard C++, and MSVC's <cmath> defines
    # it only behind this. React Native's
    # react/renderer/components/view/conversions.h uses it unguarded, seven
    # times, to convert degrees for `transform: rotate`. The same shape of
    # upstream portability bug as the missing <cstdint> above -- code that
    # compiles on Meta's toolchains and nowhere else -- and the same kind of
    # fix, on the command line rather than in their tree.
    -D_USE_MATH_DEFINES
    ${BASALT_EXCEPTIONS_FLAG}
    ${BASALT_RTTI_FLAG})
endif()


foreach(target ${RN_CORE_OBJECT_TARGETS} yogacore)
  if(TARGET ${target})
    # A few RN targets are INTERFACE libraries, which reject PRIVATE options.
    get_target_property(target_type ${target} TYPE)
    if(NOT target_type STREQUAL "INTERFACE_LIBRARY")
      target_compile_options(${target} PRIVATE ${RN_EXTRA_FLAGS})
    endif()
  endif()
endforeach()

add_library(rn_core INTERFACE)
foreach(target ${RN_CORE_OBJECT_TARGETS})
  # target_link_libraries carries usage requirements (include dirs, defines);
  # OBJECT libraries additionally need their objects named explicitly, or the
  # final link comes up short on every symbol they define.
  target_link_libraries(rn_core INTERFACE ${target})
  # Some of these (e.g. callinvoker) are header-only INTERFACE libraries with
  # no objects at all; $<TARGET_OBJECTS:> is an error on those.
  get_target_property(target_type ${target} TYPE)
  if(target_type STREQUAL "OBJECT_LIBRARY")
    target_sources(rn_core INTERFACE $<TARGET_OBJECTS:${target}>)
  endif()
endforeach()

# ---------------------------------------------------------------------------
# <TextInput>, from React Native's *iOS* variant.
#
# React Native's own rrc_textinput target globs the base sources plus
# platform/android, whose descriptor includes <fbjni/fbjni.h> and reaches into a
# Java FabricUIManager for theme padding -- unusable off Android.
#
# The iOS variant is pure C++: TextInputShadowNode measures through a
# TextLayoutManager, which on this platform is the Pango one, and nothing in it
# touches Objective-C or an Apple SDK. Its component name is "TextInput", which
# is the name packages/basalt-core' src/overrides/TextInput.js asks for.
#
# So this target takes the base sources and the iOS ones, and skips Android's.
# ---------------------------------------------------------------------------
set(RN_TEXTINPUT_DIR ${REACT_COMMON_DIR}/react/renderer/components/textinput)
file(GLOB rn_textinput_SRC CONFIGURE_DEPENDS
        ${RN_TEXTINPUT_DIR}/*.cpp
        ${RN_TEXTINPUT_DIR}/platform/ios/react/renderer/components/iostextinput/*.cpp)
add_library(rn_textinput OBJECT ${rn_textinput_SRC})
# SYSTEM, for the same reason basalt_gtk_mounting's React Native includes are: this
# project builds with -Wall -Wextra and inherits -Werror from React Native's
# own targets, and iostextinput/conversions.h has unused parameters. Plain -I
# would make every file that includes it fail to compile.
target_include_directories(rn_textinput SYSTEM PUBLIC
        ${RN_TEXTINPUT_DIR}
        ${RN_TEXTINPUT_DIR}/platform/ios
        ${REACT_COMMON_DIR})
target_link_libraries(rn_textinput
        glog folly_runtime jsi react_debug
        react_renderer_attributedstring react_renderer_componentregistry
        react_renderer_core react_renderer_graphics react_renderer_imagemanager
        react_renderer_mounting react_renderer_textlayoutmanager
        react_renderer_uimanager react_utils rrc_image rrc_text rrc_view yoga)
target_compile_options(rn_textinput PRIVATE ${BASALT_NO_UNKNOWN_PRAGMAS_FLAG} ${RN_EXTRA_FLAGS})
target_sources(rn_core INTERFACE $<TARGET_OBJECTS:rn_textinput>)
target_link_libraries(rn_core INTERFACE rn_textinput)

# ---------------------------------------------------------------------------
# Text measurement: drop React Native's stub so ours is the only definition.
#
# react/renderer/textlayoutmanager globs platform/cxx/*.cpp, which is a
# TextLayoutManager that ignores every attribute and hands back
# layoutConstraints.minimumSize. src/PangoTextLayoutManager.cpp defines the same
# symbols against Pango, so the stub has to leave the build or the two collide.
#
# The header is untouched and shared: only the implementation differs, which is
# what the platform/ split in React Native's tree is for.
# ---------------------------------------------------------------------------
get_target_property(_tlm_sources react_renderer_textlayoutmanager SOURCES)
list(REMOVE_ITEM _tlm_sources
        ${REACT_COMMON_DIR}/react/renderer/textlayoutmanager/platform/cxx/react/renderer/textlayoutmanager/TextLayoutManager.cpp)
set_target_properties(react_renderer_textlayoutmanager PROPERTIES SOURCES "${_tlm_sources}")

# ReactCommon declares jsi::dynamicFromValue (used by RawProps) but no
# CMakeLists anywhere in the tree compiles JSIDynamic.cpp. Hosts are expected
# to build it themselves.
add_library(rn_jsidynamic OBJECT ${REACT_COMMON_DIR}/jsi/jsi/JSIDynamic.cpp)
target_include_directories(rn_jsidynamic PUBLIC ${REACT_COMMON_DIR} ${REACT_COMMON_DIR}/jsi ${FOLLY_DIR})
target_link_libraries(rn_jsidynamic folly_runtime glog)
target_sources(rn_core INTERFACE $<TARGET_OBJECTS:rn_jsidynamic>)

# Same situation one directory over: ReactCxxPlatform ships a working
# boost::beast websocket client under react/http/platform/cxx, but the
# CMakeLists for react/http globs only its own directory, so nothing compiles
# it. It defines getWebSocketClientFactory(), the seam every host has to fill,
# and the packager connection is what needs it.
# It only exists from 0.83 or so. React Native 0.81 ships the IWebSocketClient
# interface with no implementation behind it, so a host on that version has no
# websocket client and therefore no packager connection: no Fast Refresh, and no
# websockets for the app either. Not a gap this build can paper over, so it is
# reported and the target skipped.
set(RN_WEBSOCKET_SRC ${REACT_CXX_PLATFORM_DIR}/react/http/platform/cxx/WebSocketClient.cpp)
if(EXISTS ${RN_WEBSOCKET_SRC})
  add_library(rn_websocket OBJECT ${RN_WEBSOCKET_SRC})
  target_include_directories(rn_websocket PUBLIC
          ${REACT_CXX_PLATFORM_DIR} ${REACT_COMMON_DIR} ${FOLLY_DIR})
  target_link_libraries(rn_websocket folly_runtime glog boost fmt double-conversion)
  target_compile_options(rn_websocket PRIVATE ${BASALT_NO_UNKNOWN_PRAGMAS_FLAG})
  target_sources(rn_core INTERFACE $<TARGET_OBJECTS:rn_websocket>)
else()
  message(STATUS "React Native ${RN_VERSION} bundles no websocket client; "
                 "the packager connection and Fast Refresh will not work")
endif()
target_link_libraries(rn_core INTERFACE yogacore folly_runtime glog boost fmt
        double-conversion fast_float)

# std::atomic<std::optional<double>> in ReactNativeFeatureFlagsAccessor is
# 16 bytes wide; on x86-64 those operations are out-of-line calls in libatomic.
# Fantom's tester CMakeLists carries the same `if(UNIX AND NOT APPLE)` branch.
if(UNIX AND NOT APPLE)
  target_link_libraries(rn_core INTERFACE atomic)
endif()

# --- The Windows flags, applied to everything React Native added -------------
#
# `-Wno-unknown-argument` and `-Wno-everything` have to land *after* React
# Native's own `-Wall -Werror -fexceptions -frtti -std=c++20`, and a compiler
# command line is built in a fixed order: CMAKE_CXX_FLAGS, then the directory's
# options, then the target's, then those a linked target exports. Only the last
# two are after React Native's, and React Native applies its flags to every
# target it defines -- ReactCxxPlatform's as much as ReactCommon's.
#
# So this walks every target in the build graph rather than a list. A list was
# tried first and covered ReactCommon but not ReactCxxPlatform, which failed
# seventy-three translation units later on -Wlanguage-extension-token inside
# folly's F14Table.h -- the same -Weverything, arriving by a route the list did
# not know about. Walking is the version that does not need updating when
# upstream adds a directory.
# Counteracting the flags was tried before removing them, and cannot work.
# `-Wno-everything` does undo `-Wall`, but React Native also applies
# `-Wpedantic`, and applies it INTERFACE on callinvoker and react_cxxstableapi
# -- so it arrives from the link graph, after every PRIVATE option, and
# re-enables -Wlanguage-extension-token on top of whatever came before. There
# is no flag that reliably lands last. Removing is the only order-independent
# answer.
#
# Both property sets, because INTERFACE_COMPILE_OPTIONS is how the ones that
# arrive last get there in the first place.
if(WIN32)
  set(RN_CLANG_ONLY_FLAGS -Wall -Werror -Wpedantic -fexceptions -frtti -std=c++20)

  function(basalt_strip_clang_flags target property)
    get_target_property(options ${target} ${property})
    if(NOT options)
      return()
    endif()
    list(REMOVE_ITEM options ${RN_CLANG_ONLY_FLAGS})
    set_target_properties(${target} PROPERTIES ${property} "${options}")
  endfunction()

  function(basalt_apply_windows_flags directory)
    get_property(targets DIRECTORY ${directory} PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target ${targets})
      get_target_property(target_type ${target} TYPE)
      if(target_type STREQUAL "UTILITY")
        continue()
      endif()
      basalt_strip_clang_flags(${target} INTERFACE_COMPILE_OPTIONS)
      # An INTERFACE library has no compilation of its own, so it has no
      # COMPILE_OPTIONS to strip and nothing to add them to.
      if(NOT target_type STREQUAL "INTERFACE_LIBRARY")
        basalt_strip_clang_flags(${target} COMPILE_OPTIONS)
        target_compile_options(${target} PRIVATE ${RN_WINDOWS_FLAGS})
      endif()
    endforeach()
    get_property(children DIRECTORY ${directory} PROPERTY SUBDIRECTORIES)
    foreach(child ${children})
      basalt_apply_windows_flags(${child})
    endforeach()
  endfunction()
  basalt_apply_windows_flags(${CMAKE_CURRENT_SOURCE_DIR})
endif()
