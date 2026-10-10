# basalt-appkit — the macOS view layer, over AppKit.
#
# A view layer, a mounting manager and a host. Everything behind them is in
# basalt-core, which this adds as a subdirectory.
#
# `appkit/` rather than `mac/`: the toolkit is what differs, and the name would
# be wrong the moment something else on macOS mattered.

cmake_minimum_required(VERSION 3.13)
project(basalt_appkit CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Executables at the top of the build tree whether this is configured directly
# or added by a wrapper, so `build/basalt_gtk` means the same thing either way.
# Every script and document here names that path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# ---------------------------------------------------------------------------
# Finding basalt-core, the shared half.
#
# The default works in both layouts this can be built from: a checkout of this
# repository, where the packages are siblings under packages/, and an app's
# node_modules, where npm hoists them side by side. `react-native run-linux --build`
# passes it explicitly anyway, because a pnpm store is neither.
#
# Guarded on the target rather than on the variable: a build that configures
# both platform packages would otherwise add the same directory twice, which
# CMake refuses.
# ---------------------------------------------------------------------------
if(NOT TARGET basalt_core)
  if(NOT DEFINED BASALT_CORE_DIR)
    set(BASALT_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../basalt-core/native)
  endif()
  get_filename_component(BASALT_CORE_DIR "${BASALT_CORE_DIR}" ABSOLUTE)
  if(NOT EXISTS ${BASALT_CORE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
      "basalt-core not found at ${BASALT_CORE_DIR}.\n"
      "Pass -DBASALT_CORE_DIR=<path to basalt-core/native>.")
  endif()
  # Capability packages first, and only here, because they contribute include
  # directories to basalt_core and CMake resolves those when the target is
  # created rather than afterwards.
  #
  # This used to live only in the repository's root CMakeLists, which is the
  # entry point for a checkout and is not the entry point for an app: the CLI
  # configures -S <host package>/native, so the loop never ran and
  # core/portability_probe.cpp failed on a Notifications.h that nothing had
  # put on the path. A checkout could not see it, because there the root added
  # the packages first and the header was always there.
  #
  # Guarded on the property rather than the variable for the same reason the
  # block around it is guarded on the target: a build configuring two host
  # packages must not add the same directory twice.
  get_property(basalt_packages_added GLOBAL PROPERTY BASALT_PACKAGES_ADDED)
  if(NOT basalt_packages_added)
    set_property(GLOBAL PROPERTY BASALT_PACKAGES_ADDED TRUE)
    foreach(package ${BASALT_PACKAGES})
      if(EXISTS ${package}/native/CMakeLists.txt)
        get_filename_component(basalt_package_name ${package} NAME)
        message(STATUS "basalt: capability package ${basalt_package_name}")
        add_subdirectory(${package}/native basalt-package-${basalt_package_name})
      endif()
    endforeach()
  endif()

  add_subdirectory(${BASALT_CORE_DIR} basalt_core)
endif()

# What capability packages contributed for this host; see
# packages/basalt-notifications for what a package sets.
#
# Read *after* the block above, because that block is where the packages are
# added. It used to be read at the top of this file, which worked for a
# checkout -- the root CMakeLists adds the packages before it adds this one --
# and silently contributed nothing for an app, where this file is the entry
# point. The symptom was a link error naming every notification seam.
get_property(BASALT_PACKAGE_APPKIT_SOURCES GLOBAL PROPERTY BASALT_PACKAGE_APPKIT_SOURCES)
# And what those sources need linked. A package contributing an implementation
# that reaches for a framework had no way to say so: this file named
# UserNotifications for the notifications package, which works only because that
# package is ours. An app's own module is not, and basalt naming its frameworks
# would be basalt knowing about it.
get_property(BASALT_PACKAGE_APPKIT_LINK_LIBRARIES GLOBAL PROPERTY BASALT_PACKAGE_APPKIT_LINK_LIBRARIES)
# And a package's own unit tests, which run in this host's suite. A capability
# with a platform half has nowhere else to be tested: it is compiled into the
# host binary, so the host's suite is the only binary that has it.
get_property(BASALT_PACKAGE_TESTS GLOBAL PROPERTY BASALT_PACKAGE_TESTS)

if(NOT APPLE)
  # Not an error. A checkout builds every package it finds, and on anything but
  # a Mac this one has nothing it could compile.
  message(STATUS "basalt-appkit skipped: not a Mac")
  return()
endif()

# Objective-C++ is enabled here rather than in the shared half, so that a Linux
# build never asks for a compiler it has no reason to have.
enable_language(OBJCXX)

# ---------------------------------------------------------------------------
# basalt_appkit_view — the AppKit view layer.
#
# No React Native dependency, so it builds and is tested on its own, on a Mac
# with nothing else installed.
# ---------------------------------------------------------------------------
add_library(basalt_appkit_view STATIC
  appkit/RnAppKitView.mm
  # The <TextInput> peer seam. In the *view* target rather than the mounting
  # one because RnAppKitView reads the peer's text for its tree dump, and this
  # target has to keep linking on its own -- the same reason GtkTextPeer.cpp
  # sits in basalt_gtk_view.
  appkit/AppKitTextPeer.mm
  # The paragraph object. Core Text only, no React Native, which is what keeps
  # this target buildable on a Mac with nothing else installed.
  appkit/RnTextLayout.mm
  appkit/AppKitSnapshot.mm)
target_compile_options(basalt_appkit_view PRIVATE -Wall -Wextra -fobjc-arc)
target_include_directories(basalt_appkit_view PUBLIC appkit)
# core/FocusRing.h and core/Backface.h, and nothing else from there. Both are
# header-only and have no React Native in them, so taking them does not make
# this target depend on the half that does -- and the alternative is a focus
# ring that is a different blue on each desktop, and three toolkits each with
# their own idea of which way a rotated card is facing.
#
# Header-only is the constraint, not a preference: this library links no core,
# so anything it takes from there has to be `inline`.
target_include_directories(basalt_appkit_view PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../basalt-core/native/core)
# CoreImage is here for one prop: an <Image>'s `blurRadius`, which Core Graphics
# has no filter of its own for. See RnBlurredImageCreate.
target_link_libraries(basalt_appkit_view PUBLIC
  "-framework Cocoa" "-framework QuartzCore" "-framework CoreText" "-framework CoreImage")

add_executable(demo_layout_appkit appkit/demo_layout_appkit.mm)
target_compile_options(demo_layout_appkit PRIVATE -Wall -Wextra -fobjc-arc)
target_link_libraries(demo_layout_appkit PRIVATE basalt_appkit_view)

# The runner comes from the shared half, which knows about no toolkit: the test
# infrastructure is shared even though the views are not.
set(BASALT_APPKIT_TEST_SOURCES
  ${BASALT_TEST_HARNESS}
  tests/test_appkit_view.mm
  # Core Text, against RnTextLayout alone. No React Native, so these run in the
  # toolkit-only build too.
  tests/test_appkit_text.mm
  tests/test_appkit_selection.mm
  # The resize modes, against a real bitmap. No React Native, so these run in
  # the toolkit-only build too.
  tests/test_appkit_image.mm
  # Dotted and dashed borders, also against a bitmap: what a border is drawn as
  # rather than what the view was told.
  tests/test_appkit_borders.mm
  # The overlay scrollbar's platform half: that it is a view above the content
  # rather than paint behind it. No React Native, so these run in the
  # toolkit-only build too.
  tests/test_appkit_scrollbar.mm
  # The NSAccessibility mapping. The React Native role name is what the
  # cross-platform diff compares; this is where the platform half is checked.
  tests/test_appkit_accessibility.mm
  # macOS's half of the platform-services seam. The alert is the reason: a
  # sheet is its own window and never appears in a snapshot.
  tests/test_appkit_services.mm
  tests/test_appkit_titlebar.mm)

# ---------------------------------------------------------------------------
# basalt_appkit_mounting — the AppKit half of the Fabric mounting manager.
#
# The mutation walk is not here: it is in the shared half, in
# core/MountingWalk.h. What is here is what is genuinely AppKit about mounting.
#
# Skipped when the build was not pointed at a React Native, so the view layer
# above still builds and tests on its own.
# ---------------------------------------------------------------------------
if(TARGET basalt_core)
  add_library(basalt_appkit_mounting STATIC
    appkit/AppKitMountingManager.mm
    # Which components this platform claims. See core/ComponentRegistry.h.
    appkit/ComponentRegistryAppKit.mm
    # macOS's implementation of core/FontRegistry.h -- the seam phase 17 named.
    # Core links against whichever implementation the platform half provides,
    # so without this nothing that reaches expo-font links.
    appkit/FontRegistryCoreText.mm
    # ...and of core/ColorScheme.h.
    appkit/ColorSchemeAppKit.mm
    # ...and of core/PlatformServices.h.
    appkit/PlatformServicesAppKit.mm
    # AttributedString -> Core Text. Shared by measurement and painting, so they
    # agree, which is the whole reason it is one file.
    appkit/CoreTextLayout.mm
    # Replaces React Native's stub TextLayoutManager; see the shared half's
    # cmake/ReactNativeCore.cmake for why the stub has to leave the build.
    appkit/CoreTextLayoutManager.mm
    # <ScrollView>: offset, clipping, onScroll, state and commands.
    appkit/AppKitScrollView.mm
    # <Image> pixels. RN's cxx ImageManager produces none; the fetch is in the
    # shared half, and only the decode and the cache are here.
    appkit/AppKitImageLoader.mm
    # <TextInput>, over a real NSTextField.
    appkit/AppKitTextInput.mm
    # Accepting a file or text dragged onto the window; see
    # core/DragAndDrop.h for the model the three hosts share.
    appkit/AppKitDropTarget.mm
    appkit/AppKitTitleBar.mm
    # A window's size, position and full-screen state; see core/WindowControl.h.
    appkit/AppKitWindowControl.mm
    # The application menu; see core/MenuModel.h for why the three differ.
    appkit/AppKitMenuBar.mm
    appkit/AppKitWindowModule.mm)
  target_compile_options(basalt_appkit_mounting PRIVATE -Wall -Wextra -fobjc-arc)
  target_include_directories(basalt_appkit_mounting PUBLIC appkit)
  target_link_libraries(basalt_appkit_mounting PUBLIC basalt_appkit_view basalt_core)
  target_link_libraries(basalt_appkit_mounting PUBLIC "-framework CoreText" "-framework ImageIO")

  # @shopify/react-native-skia, when the build was pointed at an app that has it.
  # See ../../basalt-core/native/cmake/Skia.cmake.
  #
  # Here and not on basalt_core, which is where it went first. Skia's Apple half
  # reaches NSScreen, NSColorSpace and NSApplication, and basalt_core_probe links
  # the core with no toolkit on purpose -- it exists so that what a new desktop
  # has to provide is a link error rather than a discovery. It failed on exactly
  # those three symbols, which is the probe doing its job: this is a host's
  # dependency, not a portable one.
  if(TARGET skia_core)
    target_link_libraries(basalt_appkit_mounting PUBLIC skia_core "-framework AppKit")
    target_sources(basalt_appkit_mounting PUBLIC $<TARGET_OBJECTS:skia_core>)
    target_compile_definitions(basalt_appkit_mounting PUBLIC BASALT_HAS_SKIA=1)
    # This host's half: the TurboModule the package asks for by name, and the
    # platform context it hands to Skia. See appkit/AppKitSkiaModule.h.
    target_sources(basalt_appkit_mounting PRIVATE
      appkit/AppKitSkiaModule.mm
      appkit/AppKitSkiaContext.mm
      appkit/AppKitSkiaPeer.mm)
    # That Skia renders rather than merely links; see the file. Appended here so
    # it is compiled only when there is a Skia to render with.
    list(APPEND BASALT_APPKIT_TEST_SOURCES tests/test_appkit_skia.mm)
  endif()
  # UniformTypeIdentifiers, for the file panel's filters: `allowedFileTypes`
  # took bare extensions and is deprecated, and its replacement takes UTTypes.
  target_link_libraries(basalt_appkit_mounting PUBLIC "-framework UniformTypeIdentifiers")

  # -------------------------------------------------------------------------
  # basalt_appkit — a ReactHost, Hermes, and one surface in an NSWindow.
  # -------------------------------------------------------------------------
  add_executable(basalt_appkit
    appkit/main_appkit.mm
    appkit/AppKitRunLoopObserver.mm
    appkit/AppKitAnimationChoreographer.mm
    appkit/AppKitFocus.mm
    appkit/AppKitKeyEvents.mm
    # Why macOS cannot show a notification; see the file.
    ${BASALT_PACKAGE_APPKIT_SOURCES}
    appkit/AppKitTouchDispatcher.mm)
  target_compile_options(basalt_appkit PRIVATE -Wall -Wextra -fobjc-arc)
  target_link_libraries(basalt_appkit PRIVATE basalt_appkit_mounting)
  if(BASALT_PACKAGE_APPKIT_LINK_LIBRARIES)
    target_link_libraries(basalt_appkit PRIVATE ${BASALT_PACKAGE_APPKIT_LINK_LIBRARIES})
  endif()

  # Real mutations into real views, with no JavaScript anywhere. The checkpoint
  # between "the mounting manager compiles" and "React Native runs": a mistake
  # in the Create/Insert/Update/Remove/Delete handling shows up in a picture
  # rather than in review.
  add_executable(mount_harness_appkit appkit/mount_harness_appkit.mm)
  target_compile_options(mount_harness_appkit PRIVATE -Wall -Wextra -fobjc-arc)
  target_link_libraries(mount_harness_appkit PRIVATE basalt_appkit_mounting)

  list(APPEND BASALT_APPKIT_TEST_SOURCES
    ${BASALT_CORE_TESTS}
    # A package's own tests, beside core's and for the same reason: both need
    # basalt_core, which only this branch of the build has. A view-only build --
    # no React Native, so no core -- compiles neither, rather than failing on a
    # package header it was never given the include path for.
    ${BASALT_PACKAGE_TESTS}
    tests/test_appkit_mounting.mm
    # Fonts loaded at runtime: core/FontRegistry.h's macOS half, and the
    # measurement cache a late registration has to invalidate. Needs React
    # Native, for the AttributedString a paragraph is measured from.
    tests/test_appkit_fonts.mm
    # Hit testing lives with the touch dispatcher, which the host builds; the
    # tests need it too, and it pulls in nothing else.
    appkit/AppKitFocus.mm
    appkit/AppKitKeyEvents.mm
    # Why macOS cannot show a notification; see the file.
    ${BASALT_PACKAGE_APPKIT_SOURCES}
    appkit/AppKitTouchDispatcher.mm
    tests/test_appkit_input.mm
    tests/test_appkit_key_events.mm
    # <TextInput>'s controlled-value loop, through real mutations.
    tests/test_appkit_textinput.mm
    # <ScrollView>'s props, as far as they reach without a window: the manager
    # takes a view and an emitter lookup, so the offset a prop asks for is
    # observable with no display at all.
    tests/test_appkit_scrollview.mm)
endif()

add_executable(basalt_appkit_tests ${BASALT_APPKIT_TEST_SOURCES})
target_compile_options(basalt_appkit_tests PRIVATE -Wall -Wextra -fobjc-arc)
target_include_directories(basalt_appkit_tests PRIVATE ${BASALT_TEST_INCLUDE})
# basalt_appkit_mounting already carries basalt_appkit_view; naming both makes
# the linker warn about a duplicate library rather than doing anything useful.
if(TARGET basalt_appkit_mounting)
  target_link_libraries(basalt_appkit_tests PRIVATE basalt_appkit_mounting)
else()
  target_link_libraries(basalt_appkit_tests PRIVATE basalt_appkit_view)
endif()
# The tests compile the package sources too, so they need the same libraries.
if(BASALT_PACKAGE_APPKIT_LINK_LIBRARIES)
  target_link_libraries(basalt_appkit_tests PRIVATE ${BASALT_PACKAGE_APPKIT_LINK_LIBRARIES})
endif()
