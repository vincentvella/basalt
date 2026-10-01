# The development build of this repository.
#
# Everything native lives inside the npm packages, so that installing one brings
# the host's sources with it and `react-native run-linux --build` can compile
# one. That is the same shape react-native-windows uses, where the package *is*
# the vnext directory.
#
# Three packages, and the split is the architecture rather than tidiness:
#
#   packages/basalt-core          the shared half. React Native's C++
#                                         core, Hermes, the Expo runtime, the
#                                         core modules, the mutation walk. No
#                                         toolkit, asserted by the linker.
#   packages/basalt-gtk      the Linux view layer and host.
#   packages/basalt-appkit   the macOS view layer and host.
#   packages/basalt-win32    the Windows view layer and host.
#
# Each platform package adds the shared one as a subdirectory and skips itself
# where it cannot build, so this file adds all three unconditionally and gets
# whichever of them this machine can produce.
#
# This file exists so that the way this repository has always been built keeps
# working:
#
#   cmake -B build -DRN_DIR=../react-native/packages/react-native
#   cmake --build build
#
# It contributes two things a package cannot assume: that third_party lives at
# the root of a checkout, and that `build/basalt_gtk` is where a binary should
# appear. Both are named here rather than inside a package, because inside a
# package they would be wrong for an app.

cmake_minimum_required(VERSION 3.13)
project(react_native_basalt_dev NONE)

if(NOT DEFINED BASALT_THIRD_PARTY)
  set(BASALT_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()

# RN_DIR is documented as relative to this directory, and add_subdirectory would
# otherwise resolve it against whichever package got there first.
if(DEFINED RN_DIR)
  get_filename_component(RN_DIR "${RN_DIR}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Capability packages before anything that reads what they contribute.
#
# A package appends to global properties -- sources, include directories, the
# name of its Expo installer -- and core and the hosts read those. So they have
# to be processed first, and that ordering is the only thing here that is not
# obvious from the files themselves.
#
# BASALT_PACKAGES is a list of package roots. The command-line tool passes the
# ones it found in the app's node_modules; an in-repository build defaults to
# the packages in this repository, so that developing one is the same as using
# one. See packages/basalt-notifications/native/CMakeLists.txt for
# what a package contributes.
if(NOT DEFINED BASALT_PACKAGES)
  # Named rather than globbed: the host packages and the core one live beside
  # these and are added explicitly below, so a glob over `packages/*` would add
  # them twice. A list is also the place to read what an in-repository build
  # carries.
  set(BASALT_PACKAGES
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/basalt-notifications
    ${CMAKE_CURRENT_SOURCE_DIR}/packages/basalt-subprocess)
endif()
#
# The same loop is in each host package's CMakeLists, because that is the entry
# point for an app -- the CLI configures -S <host package>/native and never
# reads this file. The property guard keeps whichever runs first from being
# undone by the other.
set_property(GLOBAL PROPERTY BASALT_PACKAGES_ADDED TRUE)
foreach(package ${BASALT_PACKAGES})
  if(EXISTS ${package}/native/CMakeLists.txt)
    get_filename_component(name ${package} NAME)
    message(STATUS "basalt: capability package ${name}")
    add_subdirectory(${package}/native basalt-package-${name})
  endif()
endforeach()

# The shared half first, explicitly. Either platform package would add it, but
# adding it here means the order the two are listed in cannot matter.
add_subdirectory(packages/basalt-core/native basalt)
add_subdirectory(packages/basalt-gtk/native basalt-gtk)
add_subdirectory(packages/basalt-appkit/native basalt-appkit)
add_subdirectory(packages/basalt-win32/native basalt-win32)
