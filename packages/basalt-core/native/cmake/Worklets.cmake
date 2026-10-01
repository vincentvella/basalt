# react-native-worklets, compiled from the app's own copy.
#
# The same arrangement as Expo.cmake and for the same reason: the package ships
# its portable half as C++ in `Common/cpp`, depending on jsi, Hermes, a call
# invoker and nothing else, so the app's own copy compiles at the app's own
# version. Nothing is vendored and nothing is pinned.
#
# What the package does *not* ship for a desktop is the host half -- the
# TurboModule, the UI scheduler, the frame queue and the platform logger are
# written per platform, and iOS's and Android's are a few hundred lines each.
# Those are core/Worklets*.{h,cpp} here.
#
# Reanimated sits on top of this and is the same shape again; see

if(NOT DEFINED BASALT_WORKLETS)
  return()
endif()

get_filename_component(WORKLETS_DIR "${BASALT_WORKLETS}" ABSOLUTE)
set(WORKLETS_CPP_DIR ${WORKLETS_DIR}/Common/cpp)

if(NOT EXISTS ${WORKLETS_CPP_DIR}/worklets/WorkletRuntime/WorkletRuntime.cpp)
  message(FATAL_ERROR
          "no react-native-worklets C++ at ${WORKLETS_CPP_DIR}.\n"
          "BASALT_WORKLETS should name an installed react-native-worklets, "
          "the one in the app's node_modules.")
endif()

# The version the package declares, which its C++ compares against the version
# its JavaScript declares and warns about when they differ. Read from the
# package rather than pinned, for the same reason everything else here is.
file(READ ${WORKLETS_DIR}/package.json WORKLETS_PACKAGE_JSON)
string(JSON WORKLETS_VERSION GET "${WORKLETS_PACKAGE_JSON}" version)

file(GLOB_RECURSE WORKLETS_SRC CONFIGURE_DEPENDS ${WORKLETS_CPP_DIR}/worklets/*.cpp)

add_library(worklets_core OBJECT ${WORKLETS_SRC})
# SYSTEM, and warnings off: somebody else's sources held to somebody else's
# warning set. This project's -Wall -Wextra -Werror is about its own code.
target_include_directories(worklets_core SYSTEM PUBLIC
        ${WORKLETS_CPP_DIR}
        ${RN_DIR}/ReactCommon
        ${RN_DIR}/ReactCommon/jsi
        ${RN_DIR}/ReactCommon/callinvoker
        ${RN_DIR}/ReactCommon/cxxreact
        ${RN_DIR}/ReactCommon/jsiexecutor
        ${RN_DIR}/ReactCommon/runtimeexecutor
        ${FOLLY_DIR})
target_compile_options(worklets_core PRIVATE -w)
target_compile_definitions(worklets_core PUBLIC
        WORKLETS_VERSION=${WORKLETS_VERSION}
        WORKLETS_FEATURE_FLAGS=\"\")
target_link_libraries(worklets_core folly_runtime glog hermes-engine::hermesvm)
if(APPLE)
  # AsyncQueueImpl wraps each job in an autorelease pool on Apple platforms,
  # which is right and needs the Objective-C runtime -- even for the GTK host,
  # which is built on macOS here and is otherwise pure C++.
  target_link_libraries(worklets_core "-framework Foundation")
endif()

message(STATUS "Worklets from ${WORKLETS_DIR}")

# Not add_compile_definitions: that is directory-scoped, and the hosts are
# sibling directories since the packages were split, so they would compile
# without it and quietly not offer the module. It goes on basalt_core as PUBLIC
# instead, where linking carries it -- the same fix BASALT_RN_MINOR needed.
# See the target_compile_definitions beside basalt_core in ../CMakeLists.txt.
