# react-native-basalt — everything that is not about a particular toolkit.
#
# The bedrock the platform packages stand on: React Native's C++ core, Hermes,
# the Expo runtime, the core TurboModules, networking, and the Fabric mutation
# walk. No GTK, no AppKit, no window. react-native-basalt-gtk and
# react-native-basalt-appkit each add a view layer on top of this and nothing
# else.
#
# Configured on its own it builds `basalt_core` and the probe that proves the
# claim. A platform package adds it as a subdirectory; see BASALT_CORE_DIR in
# either of theirs.

cmake_minimum_required(VERSION 3.13)
project(basalt_core CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Executables at the top of the build tree whether this is configured directly
# or added by a wrapper, so `build/basalt_gtk` means the same thing either way.
# Every script and document here names that path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Where folly, fast_float, nlohmann, Hermes and the codegen output live.
#
# Not inside this directory. In a development checkout they sit at the root of
# the repository, and when an app builds a host they belong to that app, not to
# a copy of this package inside its node_modules -- npm would delete them on the
# next install, and rebuilding Hermes is not a thing to lose by accident.
# bootstrap.sh takes the same path and fills it.
if(NOT DEFINED BASALT_THIRD_PARTY)
  set(BASALT_THIRD_PARTY ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
endif()
get_filename_component(BASALT_THIRD_PARTY "${BASALT_THIRD_PARTY}" ABSOLUTE)

# The test runner, which knows about no toolkit. Each platform package compiles
# it alongside its own tests and brings its own `main`; see docs/TESTING.md.
set(BASALT_TEST_HARNESS ${CMAKE_CURRENT_SOURCE_DIR}/tests/TestHarness.cpp
    CACHE INTERNAL "the toolkit-free test runner")
set(BASALT_TEST_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/tests
    CACHE INTERNAL "where TestHarness.h lives")

# Tests for core's own behaviour, compiled into every platform's suite.
#
# They belong to neither platform, so asserting them in one would leave the
# other unchecked and asserting them in both would be the same file twice. The
# seam they sit on top of -- what the system is actually set to -- is never
# asked about, because it differs by machine.
set(BASALT_CORE_TESTS
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_app_identity.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scroll_animation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_backface.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_drag_and_drop.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_quit_file.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_settle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_key_events.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_js_runtime_access.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_image_cache.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scroll_bounds.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scroll_indicator.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_scroll_snap.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_window_close.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_window_limits.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_appearance.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_blobs.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_controls.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_devbundle.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_gestures.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_hover.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_momentum.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_share.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/test_sourcecode.cpp
    CACHE INTERNAL "core's own tests")

# ---------------------------------------------------------------------------
# React Native's C++ core, and the half of this project that is not a toolkit.
#
# Everything from here to the end of the block is platform-independent, which is
# why it comes before either view layer rather than inside one. Needs React
# Native's C++ core and its third-party dependencies (folly, glog, boost, fmt,
# double-conversion). Enable with:
#
#   cmake -B build -DRN_DIR=/path/to/react-native/packages/react-native
#
# See README.md for the current state of these targets.
# ---------------------------------------------------------------------------

if(DEFINED RN_DIR)
  # RN's own CMakeLists include files by ${REACT_COMMON_DIR}/..., which a
  # relative path resolves against the wrong directory. Anchor it to this repo.
  get_filename_component(RN_DIR "${RN_DIR}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}")
  if(NOT DEFINED FOLLY_DIR)
    set(FOLLY_DIR ${BASALT_THIRD_PARTY}/folly)
  endif()
  if(NOT DEFINED FAST_FLOAT_DIR)
    set(FAST_FLOAT_DIR ${BASALT_THIRD_PARTY}/fast_float)
  endif()
  if(NOT DEFINED NLOHMANN_JSON_DIR)
    set(NLOHMANN_JSON_DIR ${BASALT_THIRD_PARTY}/nlohmann_json)
  endif()
  if(NOT DEFINED CODEGEN_DIR)
    set(CODEGEN_DIR ${BASALT_THIRD_PARTY}/codegen)
  endif()
  if(NOT DEFINED HERMES_DIR)
    set(HERMES_DIR ${BASALT_THIRD_PARTY}/hermes)
  endif()
  if(NOT DEFINED HERMES_BUILD_DIR)
    set(HERMES_BUILD_DIR ${BASALT_THIRD_PARTY}/hermes-build)
  endif()

  # React Native's npm package omits ReactCxxPlatform, so with an installed
  # React Native it is fetched into third_party at the matching version instead.
  # bootstrap.sh does the fetching; this just says where to look.
  if(NOT EXISTS ${RN_DIR}/ReactCxxPlatform AND EXISTS ${BASALT_THIRD_PARTY}/ReactCxxPlatform)
    set(RN_CXX_PLATFORM_OVERRIDE ${BASALT_THIRD_PARTY}/ReactCxxPlatform)
    message(STATUS "ReactCxxPlatform from ${RN_CXX_PLATFORM_OVERRIDE}")
  endif()

  # Expo's core, compiled from the app's own expo-modules-core when the build
  # was pointed at one. Point BASALT_EXPO_MODULES_CORE at
  # <app>/node_modules/expo-modules-core; without it an Expo app fails at its
  # first import exactly as before, and a plain React Native app is unaffected.
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Expo.cmake)

  # First: everything below spells compiler flags through the names this
  # defines, because clang's command line and MSVC's share almost none of them.
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/CompilerFlags.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ThirdParty.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/ReactNativeCore.cmake)

  # react-native-worklets, which Reanimated is built on. After ReactNativeCore
  # rather than beside Expo: it needs Hermes and React Native's own headers,
  # which that file is what sets up.
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Worklets.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Reanimated.cmake)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Skia.cmake)

  # ---------------------------------------------------------------------------
  # basalt_core — everything that is not about a particular toolkit.
  #
  # A desktop platform on React Native's C++ host divides in two. One half is
  # the view layer: widgets, text shaping, input, the frame clock. That half is
  # GTK here and would be something else on macOS or Windows, and there is no
  # sense pretending otherwise. The other half is the host wiring, the Expo
  # runtime, the core modules and networking, and none of it is about Linux at
  # all -- it was simply written here first.
  #
  # This target is the second half, and it deliberately does not link GTK. That
  # is the whole point: the split is asserted by the compiler rather than by a
  # directory name, so a toolkit dependency that creeps into core fails the
  # build rather than quietly making core unshareable.
  # ---------------------------------------------------------------------------
  # What the discovered capability packages contribute, and the translation unit
  # that lets core call them without naming them. Generated either way: with no
  # packages it defines an empty function, so core has one thing to call rather
  # than a conditional. See core/PackageModules.h.
  get_property(basalt_package_installers GLOBAL PROPERTY BASALT_PACKAGE_EXPO_INSTALLERS)
  get_property(basalt_package_core_sources GLOBAL PROPERTY BASALT_PACKAGE_CORE_SOURCES)
  get_property(basalt_package_includes GLOBAL PROPERTY BASALT_PACKAGE_INCLUDES)

  # A semicolon by character code: CMake treats `;` as a list separator, and
  # escaping it as `\;` survives into the generated file rather than becoming
  # one. This is the shortest way to emit C++ from CMake without fighting that.
  string(ASCII 59 basalt_semicolon)
  set(basalt_package_declarations "")
  set(basalt_package_calls "")
  foreach(installer ${basalt_package_installers})
    string(APPEND basalt_package_declarations
      "void ${installer}(facebook::jsi::Runtime &, facebook::jsi::Object &)${basalt_semicolon}\n")
    string(APPEND basalt_package_calls
      "  ${installer}(runtime, modules)${basalt_semicolon}\n")
  endforeach()
  configure_file(${CMAKE_CURRENT_SOURCE_DIR}/core/PackageModules.cpp.in
                 ${CMAKE_CURRENT_BINARY_DIR}/BasaltPackageModules.cpp @ONLY)

  add_library(basalt_core STATIC
    ${CMAKE_CURRENT_BINARY_DIR}/BasaltPackageModules.cpp
    ${basalt_package_core_sources}
    # What the app calls itself, which a generic host binary cannot know.
    core/AppIdentity.cpp
    # An animated scrollTo, which is a curve rather than a jump.
    core/ScrollAnimation.cpp
    # Where a paged or snapping scroll comes to rest.
    core/DragAndDrop.cpp
    core/ImageCache.cpp
    core/ScrollBounds.cpp
    core/ScrollIndicator.cpp
    core/ScrollSnap.cpp
    # `Appearance`, and the portable half of light-vs-dark.
    core/AppearanceModule.cpp
    # Blob, File and FileReader -- portable in full.
    core/BlobModule.cpp
    core/BlobRegistry.cpp
    core/ColorScheme.cpp
    # Clipboard, Vibration, Alert, Linking, I18nManager, AccessibilityInfo --
    # the core modules ReactCxxPlatform does not provide.
    core/CoreModules.cpp
    # <ActivityIndicator>, <Switch>, <RefreshControl> and <Modal>: the props
    # three view layers have to read identically, plus the two shadow-node
    # obligations React Native leaves to Objective-C. See the header.
    core/DesktopControls.cpp
    # React Native's developer menu, which on a desktop is a keyboard shortcut
    # into a native popup rather than a shake.
    core/DevMenu.cpp
    # The native file dialogs, which React Native has no API for at all.
    core/DialogModule.cpp
    # The application menu: the description, and the JavaScript boundary. What
    # each platform does with it is in its own MenuBar file.
    core/MenuModule.cpp
    # React DevTools' element highlighter: the rectangles, parsed once.
    core/DebuggingOverlay.cpp
    core/DevBundle.cpp
    core/ExpoImageComponent.cpp
    core/GestureHandlerModule.cpp
    core/Gestures.cpp
    # Hover: the pointer-event path React Native only has on Android.
    core/HoverTracker.cpp
    # React Native's error inspector, which is a surface rather than a widget.
    core/LogBoxSurface.cpp
    core/ExpoModules.cpp
    core/ExpoRuntime.cpp
    # The JavaScript thread, for native code that is not a TurboModule and so
    # was handed no call invoker; see the header.
    core/JsRuntimeAccess.cpp
    core/HttpClient.cpp
    # The bytes behind an <Image>, which is the half of image loading that is
    # the same on every desktop.
    core/ImageBytes.cpp
    core/PlatformConstantsModule.cpp
    # The wheel past the top of a list, which is this platform's pull.
    core/PullToRefresh.cpp
    # The fling GTK hands over half-finished; see the header.
    core/ScrollMomentum.cpp
    # A share sheet for the desktops that have none; see the header.
    core/ShareFallback.cpp
    core/SourceCodeModule.cpp
    # Answering a modal dialog without showing one; see the header.
    core/TestDialog.cpp
    # Quitting gracefully when the harness says so, rather than on a budget
    # fixed before launch; see the header.
    core/TestQuitFile.cpp
    # What a <Canvas> from @shopify/react-native-skia mounts; see the header.
    core/SkiaPictureViewComponent.cpp
    # globalThis.__turboModuleProxy, which a bridgeless runtime does not get; see
    # the header.
    core/TurboModuleProxy.cpp
    # Which view claims which key; see the header.
    core/KeyEvents.cpp
    # BASALT_TEST_KEY, parsed once rather than in three hosts; see the header.
    core/TestKeys.cpp
    core/StatusBarModule.cpp
    core/ReanimatedModule.cpp
    core/UIManagerAccess.cpp
    # A window's size, position and full-screen state: the JavaScript boundary
    # written once, over a four-function per-platform seam.
    core/WindowMethods.cpp
    # The bounds cache behind getBounds(), which cannot ask the toolkit.
    core/WindowBoundsCache.cpp
    # Opening a second window, which is opening a second surface.
    core/WindowsModule.cpp
    core/WorkletsModule.cpp)
  target_compile_options(basalt_core PRIVATE -Wall -Wextra)
  # The same upstream portability bug ReactNativeCore.cmake force-includes
  # around for React Native's own targets, reaching our translation units too:
  # ReactCxxPlatform's react/runtime/ReactInstanceConfig.h declares a
  # `uint32_t` and includes no <cstdint>, so anything of ours that includes it
  # -- core/DevMenu.cpp does -- fails to compile.
  #
  # PUBLIC, so the host libraries that include React Native's headers get it
  # as well, for the same reason the definitions below are.
  #
  # Only React Native 0.86 on Linux shows it, which is why it took a release
  # workflow to find. At 0.87 the header also includes <functional> and
  # <memory>, which drag <cstdint> in on libstdc++; 0.86 includes only
  # <string>, which does not. On a Mac libc++ supplies it either way, so the
  # same app built here and on Linux disagreed about whether React Native
  # compiles.
  target_compile_options(basalt_core PUBLIC ${BASALT_FORCE_INCLUDE_CSTDINT_FLAG})
  # The React Native version, for the version guards and PlatformConstants.
  #
  # ReactNativeCore.cmake also states these with add_compile_definitions, which
  # is how React Native's own targets get them -- but that is directory-scoped,
  # and the platform packages are sibling directories rather than children. On
  # the target they travel with the link, so anything using the core has them.
  target_compile_definitions(basalt_core PUBLIC
    BASALT_REACT_NATIVE_VERSION="${RN_VERSION}"
    BASALT_RN_MINOR=${RN_MINOR})
  target_include_directories(basalt_core PUBLIC core ${basalt_package_includes})
  target_include_directories(basalt_core SYSTEM PUBLIC
    ${RN_DIR}/ReactCommon
    ${RN_DIR}/ReactCommon/jsi
    ${REACT_CXX_PLATFORM_DIR}
    ${FOLLY_DIR}
    ${CODEGEN_DIR}
    ${CODEGEN_DIR}/react/renderer/components/FBReactNativeSpec
    ${CODEGEN_DIR}/react/renderer/components
    # <Switch>'s shadow node and descriptor, which React Native files under
    # "iosswitch" and which are ordinary portable C++ apart from the one
    # measurement core/DesktopControls.cpp supplies. Android's needs fbjni;
    # this one needs nothing.
    ${RN_DIR}/ReactCommon/react/renderer/components/switch/iosswitch)
  # libcurl, because core/HttpClient.cpp is here. It used to be named only on
  # basalt_gtk_mounting, which worked for exactly as long as the GTK host was the only
  # thing linking core: the archive member goes unreferenced until something
  # asks for an http client, so the missing dependency stayed invisible until
  # the macOS host asked.
  target_link_libraries(basalt_core PUBLIC rn_core CURL::libcurl)
  if(TARGET expo_core)
    target_link_libraries(basalt_core PUBLIC expo_core)
    target_sources(basalt_core PUBLIC $<TARGET_OBJECTS:expo_core>)
  endif()
  if(TARGET reanimated_core)
    target_link_libraries(basalt_core PUBLIC reanimated_core)
    target_sources(basalt_core PUBLIC $<TARGET_OBJECTS:reanimated_core>)
    target_compile_definitions(basalt_core PUBLIC BASALT_HAS_REANIMATED=1)
  endif()
  if(TARGET worklets_core)
    target_link_libraries(basalt_core PUBLIC worklets_core)
    target_sources(basalt_core PUBLIC $<TARGET_OBJECTS:worklets_core>)
    # PUBLIC, so the hosts get it too: they are sibling directories now, and a
    # directory-scoped definition would leave them compiling the provider
    # without its worklets branch and quietly not offering the module.
    target_compile_definitions(basalt_core PUBLIC BASALT_HAS_WORKLETS=1)
  endif()

  # Links the core and no toolkit. See core/portability_probe.cpp: what it
  # cannot find is precisely what a new desktop platform has to provide.
  #
  # Capability packages contribute their own stubs: their core half is
  # compiled into basalt_core, so their seams are symbols this has to satisfy,
  # and the probe cannot include their headers itself -- an app installs the
  # packages it wants, and core must compile in one that installed none.
  get_property(basalt_package_probe_sources GLOBAL PROPERTY BASALT_PACKAGE_PROBE_SOURCES)
  add_executable(basalt_core_probe core/portability_probe.cpp ${basalt_package_probe_sources})
  target_link_libraries(basalt_core_probe PRIVATE basalt_core)

endif()
