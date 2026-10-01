# react-native-reanimated, compiled from the app's own copy.
#
# The same arrangement as Worklets.cmake, which this needs: Reanimated 4 is a
# layer on react-native-worklets, and its animations run in the worklet runtime
# that package provides.
#
# Reanimated's portable C++ is portable to exactly two platforms. Unlike
# worklets, it branches on `ANDROID` and `__APPLE__` in about ninety places, and
# a few of those decide a *type* rather than an implementation -- on a third
# platform the type is simply never declared and the struct that uses it does
# not compile. `ReanimatedCompat.h` is force-included ahead of everything to
# fill those in; see its header for what and why.

if(NOT DEFINED BASALT_REANIMATED)
  return()
endif()

if(NOT TARGET worklets_core)
  message(FATAL_ERROR
          "BASALT_REANIMATED needs BASALT_WORKLETS too: Reanimated 4 is built "
          "on react-native-worklets and does not compile without it.")
endif()

get_filename_component(REANIMATED_DIR "${BASALT_REANIMATED}" ABSOLUTE)
set(REANIMATED_CPP_DIR ${REANIMATED_DIR}/Common/cpp)

if(NOT EXISTS ${REANIMATED_CPP_DIR}/reanimated/NativeModules/ReanimatedModuleProxy.cpp)
  message(FATAL_ERROR
          "no react-native-reanimated C++ at ${REANIMATED_CPP_DIR}.\n"
          "BASALT_REANIMATED should name an installed react-native-reanimated, "
          "the one in the app's node_modules.")
endif()

file(READ ${REANIMATED_DIR}/package.json REANIMATED_PACKAGE_JSON)
string(JSON REANIMATED_VERSION GET "${REANIMATED_PACKAGE_JSON}" version)

# Reanimated's own codegen artifacts.
#
# Its C++ includes `react/renderer/components/rnreanimated/Props.h`, which does
# not exist in the package: React Native generates it from the TypeScript specs
# during a library's native build, and there is no such build here. So this runs
# the same generator React Native's own Gradle and CocoaPods scripts run,
# against the package's own codegenConfig, at configure time.
#
# It is the first time this project has run codegen for something other than
# React Native itself, and it is what a real autolinking step would do.
set(REANIMATED_CODEGEN ${CMAKE_BINARY_DIR}/codegen-reanimated)
set(REANIMATED_CODEGEN_JNI ${REANIMATED_CODEGEN}/android/app/build/generated/source/codegen/jni)
set(REANIMATED_CODEGEN_STAMP ${REANIMATED_CODEGEN}/.version)

if(NOT EXISTS ${REANIMATED_CODEGEN_STAMP} OR
   NOT "${REANIMATED_VERSION}" STREQUAL "$CACHE{BASALT_REANIMATED_CODEGEN_VERSION}")
  message(STATUS "Generating codegen artifacts for reanimated ${REANIMATED_VERSION}")
  file(REMOVE_RECURSE ${REANIMATED_CODEGEN})
  execute_process(
    COMMAND node scripts/generate-codegen-artifacts.js
            -p ${REANIMATED_DIR} -t android -o ${REANIMATED_CODEGEN} -s library -f
    WORKING_DIRECTORY ${RN_DIR}
    RESULT_VARIABLE REANIMATED_CODEGEN_RESULT
    OUTPUT_QUIET)
  if(NOT EXISTS ${REANIMATED_CODEGEN_JNI}/react/renderer/components/rnreanimated/Props.h)
    message(FATAL_ERROR
            "codegen produced no rnreanimated artifacts in ${REANIMATED_CODEGEN} "
            "(exit ${REANIMATED_CODEGEN_RESULT})")
  endif()
  file(WRITE ${REANIMATED_CODEGEN_STAMP} "${REANIMATED_VERSION}")
  set(BASALT_REANIMATED_CODEGEN_VERSION ${REANIMATED_VERSION}
      CACHE INTERNAL "the reanimated the codegen artifacts were generated from")
endif()

# Only the *component* artifacts -- the props, shadow nodes and descriptors
# Reanimated's own C++ includes.
#
# Not `rnreanimated-generated.cpp` or `rnworklets-generated.cpp`, which are the
# JNI half of a TurboModule spec and include <ReactCommon/JavaTurboModule.h>.
# The modules themselves are hand-written here (core/WorkletsModule.h says why),
# and neither one needs Java.
#
# Not FBReactNativeSpec either, which comes out of the same run because codegen
# walks the dependency tree and which third_party/codegen already builds.
# Compiling this copy too is two definitions of every symbol in it.
file(GLOB REANIMATED_CODEGEN_SRC CONFIGURE_DEPENDS
        ${REANIMATED_CODEGEN_JNI}/react/renderer/components/rnreanimated/*.cpp)

file(GLOB_RECURSE REANIMATED_SRC CONFIGURE_DEPENDS ${REANIMATED_CPP_DIR}/reanimated/*.cpp)
list(APPEND REANIMATED_SRC ${REANIMATED_CODEGEN_SRC})

add_library(reanimated_core OBJECT ${REANIMATED_SRC})
target_include_directories(reanimated_core SYSTEM PUBLIC
        ${REANIMATED_CPP_DIR}
        ${REANIMATED_CODEGEN_JNI}
        ${CMAKE_CURRENT_SOURCE_DIR}/core
        ${RN_DIR}/ReactCommon
        ${RN_DIR}/ReactCommon/jsi
        ${RN_DIR}/ReactCommon/callinvoker
        ${RN_DIR}/ReactCommon/cxxreact
        ${RN_DIR}/ReactCommon/jsiexecutor
        ${RN_DIR}/ReactCommon/runtimeexecutor
        ${RN_DIR}/ReactCommon/yoga
        ${FOLLY_DIR})
target_link_libraries(reanimated_core worklets_core folly_runtime glog hermes-engine::hermesvm)
# Somebody else's sources, held to somebody else's warning set.
target_compile_options(reanimated_core PRIVATE -w
        -include ${CMAKE_CURRENT_SOURCE_DIR}/core/ReanimatedCompat.h)
target_compile_definitions(reanimated_core PUBLIC
        REANIMATED_VERSION=${REANIMATED_VERSION})

message(STATUS "Reanimated from ${REANIMATED_DIR}")
