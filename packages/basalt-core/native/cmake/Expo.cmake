# Expo's core runtime, from the app's own expo-modules-core.
#
# Nothing is vendored and nothing is fetched. expo-modules-core ships its
# portable C++ in the npm package, under common/cpp, and it depends on JSI and a
# call invoker and nothing else -- no JNI, no Objective-C. So the app's own copy
# compiles, at the app's own Expo version, which keeps Expo's C++ and JavaScript
# in step for the same reason building against the app's React Native does.
#
# See src/ExpoRuntime.cpp for what is installed and what is deliberately not.

if(NOT DEFINED BASALT_EXPO_MODULES_CORE)
  return()
endif()

get_filename_component(EXPO_CORE_DIR "${BASALT_EXPO_MODULES_CORE}" ABSOLUTE)
set(EXPO_CPP_DIR ${EXPO_CORE_DIR}/common/cpp)

if(NOT EXISTS ${EXPO_CPP_DIR}/EventEmitter.cpp)
  message(FATAL_ERROR
          "no expo-modules-core C++ at ${EXPO_CPP_DIR}.\n"
          "BASALT_EXPO_MODULES_CORE should name an installed "
          "expo-modules-core, the one in the app's node_modules.")
endif()

# The runtime classes and their JSI helpers, and deliberately not everything
# under common/cpp. `fabric/` holds Expo's own Fabric view components, which are
# a separate port with a separate purpose: they need the whole renderer include
# set, and nothing installs them here because no Expo view exists on this
# platform. `tests/` brings a test framework this build has no reason to have.
file(GLOB EXPO_CORE_SRC CONFIGURE_DEPENDS
        ${EXPO_CPP_DIR}/*.cpp
        ${EXPO_CPP_DIR}/JSI/*.cpp)
list(FILTER EXPO_CORE_SRC EXCLUDE REGEX "/tests?/")

# On Windows, a copy with `#import` spelled `#include`.
#
# expo-modules-core's C++ is written for clang and GCC, where `#import` is an
# include that happens once. clang-cl reads it the way MSVC does -- as a COM
# type-library import -- and refuses it ("#import of type library is an
# unsupported Microsoft feature"), and nothing turns that reading off without
# also turning off the Microsoft compatibility the Windows SDK's headers need.
# In the expo-modules-core that SDK 57 installs it is one line, in
# JSI/ObjectDeallocator.h, and every file that includes that header fails with
# it -- this platform's own ExpoRuntime.cpp included, which is why the copy is
# also what goes on the include path.
#
# A copy in the build tree rather than an edit to node_modules, which npm
# rewrites on every install. Both directories are copied whole, headers and
# all, because a quoted include is looked for beside the file that includes it
# before any include path is consulted. Every header already carries
# `#pragma once`, so `#include` loses nothing `#import` gave. Worth reporting
# upstream: a C++ header has no need of an Objective-C directive.
if(WIN32)
  set(EXPO_CPP_COPY ${CMAKE_CURRENT_BINARY_DIR}/expo-modules-core-cpp)
  file(GLOB EXPO_CPP_FILES CONFIGURE_DEPENDS
          ${EXPO_CPP_DIR}/*.h ${EXPO_CPP_DIR}/*.cpp
          ${EXPO_CPP_DIR}/JSI/*.h ${EXPO_CPP_DIR}/JSI/*.cpp)
  foreach(source ${EXPO_CPP_FILES})
    file(RELATIVE_PATH relative ${EXPO_CPP_DIR} ${source})
    file(READ ${source} contents)
    string(REGEX REPLACE "(^|\n)([ \t]*)#([ \t]*)import([ \t])" "\\1\\2#\\3include\\4" contents "${contents}")
    # Only written when it differs, so a reconfigure does not touch every
    # object's timestamp and rebuild them all.
    set(copy ${EXPO_CPP_COPY}/${relative})
    set(existing "")
    if(EXISTS ${copy})
      file(READ ${copy} existing)
    endif()
    if(NOT existing STREQUAL contents)
      file(WRITE ${copy} "${contents}")
    endif()
  endforeach()
  list(TRANSFORM EXPO_CORE_SRC REPLACE "^${EXPO_CPP_DIR}" "${EXPO_CPP_COPY}")
  set(EXPO_CPP_DIR ${EXPO_CPP_COPY})
endif()

# `ExpoModulesJSI/JSIUtils.h`, which is how expo-modules-core's own sources have
# included their JSI helpers since version 55.
#
# The files are still at `common/cpp/JSI/`; what changed is that upstream split
# them into a second CocoaPods pod named ExpoModulesJSI, and CocoaPods publishes
# a pod's headers under its own name. A build that is not CocoaPods has to make
# that prefix resolve itself, which is one directory with one link in it -- and
# adding `common/cpp/JSI` to the include path, as this already does, does not
# help, because the include names the prefix.
#
# Only built when the prefix does not already resolve, so an older
# expo-modules-core is untouched.
if(EXISTS ${EXPO_CPP_DIR}/JSI/JSIUtils.h AND NOT EXISTS ${EXPO_CPP_DIR}/ExpoModulesJSI/JSIUtils.h)
  set(EXPO_JSI_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/expo-modules-jsi-include)
  file(MAKE_DIRECTORY ${EXPO_JSI_PREFIX})
  if(WIN32)
    # No symlink without developer mode; the headers are small and this is the
    # same copy-into-the-build-tree the #import rewrite above already does.
    file(COPY ${EXPO_CPP_DIR}/JSI/ DESTINATION ${EXPO_JSI_PREFIX}/ExpoModulesJSI
         FILES_MATCHING PATTERN "*.h")
  else()
    # Removed and remade rather than created when absent: pointing
    # BASALT_EXPO_MODULES_CORE at a different install would otherwise leave the
    # link aimed at the old one, and the same header would then be included
    # under two paths and redefine everything in it.
    file(REMOVE ${EXPO_JSI_PREFIX}/ExpoModulesJSI)
    file(CREATE_LINK ${EXPO_CPP_DIR}/JSI ${EXPO_JSI_PREFIX}/ExpoModulesJSI SYMBOLIC)
  endif()
endif()

add_library(expo_core OBJECT ${EXPO_CORE_SRC})
# SYSTEM, and warnings off: these are somebody else's sources held to somebody
# else's warning set, and this project's -Wall -Wextra is about its own code.
target_include_directories(expo_core SYSTEM PUBLIC
        ${EXPO_CPP_DIR} ${EXPO_CPP_DIR}/JSI ${EXPO_JSI_PREFIX}
        ${RN_DIR}/ReactCommon ${RN_DIR}/ReactCommon/jsi ${FOLLY_DIR})
target_compile_options(expo_core PRIVATE -w)
# TypedArray.cpp throws std::runtime_error without including <stdexcept>.
# libstdc++ and libc++ happen to pull it in through <string>; Microsoft's
# standard library does not. Force-included rather than patched into the copy
# above, because it is a missing include rather than a wrong directive, and the
# next file to make the same assumption will not need its own fix.
if(MSVC)
  target_compile_options(expo_core PRIVATE /FIstdexcept)
endif()
target_link_libraries(expo_core folly_runtime glog)

message(STATUS "Expo core from ${EXPO_CORE_DIR}")
add_compile_definitions(BASALT_HAS_EXPO=1)
