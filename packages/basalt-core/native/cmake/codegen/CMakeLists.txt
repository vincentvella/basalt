# Replaces the CMakeLists that generate-codegen-artifacts.js emits alongside
# the generated sources, which links Android-only targets (fbjni,
# turbomodulejsijni). Mirrors private/react-native-fantom/tester/codegen/
# CMakeLists.txt. bootstrap.sh regenerates the sources around this file and
# must not overwrite it.

cmake_minimum_required(VERSION 3.13)

# Only the Fabric component specs. The top-level FBReactNativeSpec-generated.cpp
# is the JNI TurboModule binding and includes ReactCommon/JavaTurboModule.h,
# which is Android-only. Fantom excludes it the same way.
file(GLOB react_codegen_SRCS CONFIGURE_DEPENDS
  react/renderer/components/FBReactNativeSpec/*.cpp)

add_library(react_codegen_rncore OBJECT ${react_codegen_SRCS})

target_include_directories(react_codegen_rncore PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/react/renderer/components/FBReactNativeSpec
  ${CMAKE_CURRENT_SOURCE_DIR}/react/renderer/components)

target_link_libraries(react_codegen_rncore
  jsi
  react_nativemodule_core
  rrc_view)

target_compile_options(react_codegen_rncore PRIVATE
  -DLOG_TAG=\"ReactNative\"
  -fexceptions
  -frtti
  -std=c++20
  -Wall)
