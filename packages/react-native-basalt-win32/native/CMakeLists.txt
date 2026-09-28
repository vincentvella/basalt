# react-native-basalt-win32 — the Windows view layer, over Win32 and Direct2D.
#
# A view layer, a mounting manager and a host. Everything behind them is in
# react-native-basalt, which this adds as a subdirectory.
#
# `win32/` rather than `windows/`: the toolkit is what differs, and the name
# would be wrong the moment WinUI or something after it mattered. The npm
# package is `-win32` for the same reason `-appkit` is not `-macos`, and neither
# name is the JavaScript platform's -- that one is `windows`, matching
# react-native-windows, and lives in the shared package.

cmake_minimum_required(VERSION 3.13)
project(basalt_win32 CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Executables at the top of the build tree whether this is configured directly
# or added by a wrapper, so `build/basalt_win32` means the same thing either
# way. Every script and document here names that path.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
# ...including under the multi-config generators, which are the default on
# Windows: Visual Studio and Ninja Multi-Config both append Debug/ or Release/
# to the directory above unless told otherwise, and a document that says
# `build/basalt_win32_tests` would be wrong on the platform it was written for.
foreach(config ${CMAKE_CONFIGURATION_TYPES})
  string(TOUPPER ${config} config_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR})
endforeach()

# ---------------------------------------------------------------------------
# Finding react-native-basalt, the shared half.
#
# The default works in both layouts this can be built from: a checkout of this
# repository, where the packages are siblings under packages/, and an app's
# node_modules, where npm hoists them side by side.
#
# Guarded on the target rather than on the variable: a build that configures
# more than one platform package would otherwise add the same directory twice,
# which CMake refuses.
# ---------------------------------------------------------------------------
if(NOT TARGET basalt_core)
  if(NOT DEFINED BASALT_CORE_DIR)
    set(BASALT_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../react-native-basalt/native)
  endif()
  get_filename_component(BASALT_CORE_DIR "${BASALT_CORE_DIR}" ABSOLUTE)
  if(NOT EXISTS ${BASALT_CORE_DIR}/CMakeLists.txt)
    message(FATAL_ERROR
      "react-native-basalt not found at ${BASALT_CORE_DIR}.\n"
      "Pass -DBASALT_CORE_DIR=<path to react-native-basalt/native>.")
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
# packages/react-native-basalt-notifications for what a package sets.
#
# Read *after* the block above, because that block is where the packages are
# added. It used to be read at the top of this file, which worked for a
# checkout -- the root CMakeLists adds the packages before it adds this one --
# and silently contributed nothing for an app, where this file is the entry
# point. The symptom was a link error naming every notification seam.
get_property(BASALT_PACKAGE_WIN32_SOURCES GLOBAL PROPERTY BASALT_PACKAGE_WIN32_SOURCES)
# And what those sources need linked. A package contributing an implementation
# that reaches for a library had no way to say so; see the same property in the
# AppKit package for what went missing.
get_property(BASALT_PACKAGE_WIN32_LINK_LIBRARIES GLOBAL PROPERTY BASALT_PACKAGE_WIN32_LINK_LIBRARIES)

if(NOT WIN32)
  # Not an error. A checkout builds every package it finds, and on anything but
  # Windows this one has nothing it could compile.
  message(STATUS "react-native-basalt-win32 skipped: not Windows")
  return()
endif()

# ---------------------------------------------------------------------------
# What this project's own sources are held to.
#
# The GTK and AppKit halves say `-Wall -Wextra`, which MSVC does not speak.
# /W4 is the nearest equivalent and /permissive- is the part that matters more:
# without it MSVC accepts things no other compiler in this project would, and
# the first anyone would hear of it is the Linux build failing on code that
# compiled here. Set on targets rather than globally, so React Native's own
# sources -- which set their own flags and are not warning-clean under /W4 --
# never inherit them.
#
# Note that clang-cl reports as MSVC here, which is intended: it takes the same
# flags, and `docs/DECISIONS.md` prefers clang where there is a choice.
# ---------------------------------------------------------------------------
if(MSVC)
  set(BASALT_WIN32_WARNINGS /W4 /permissive-)
else()
  set(BASALT_WIN32_WARNINGS -Wall -Wextra)
endif()

# UNICODE, because every Win32 entry point in this project takes wide strings
# and the ANSI variants silently lose anything outside the active code page --
# which for React Native, where a string is UTF-8 from JavaScript, is most of
# the world. NOMINMAX and WIN32_LEAN_AND_MEAN because windows.h otherwise
# defines `min` and `max` as macros and breaks <algorithm> at a distance.
#
# _CRT_SECURE_NO_WARNINGS is not laziness about a real hazard: what it silences
# here is C4996 on `std::getenv`, which the other two platforms call in the same
# place for the same reason and which has no portable replacement. Anything that
# genuinely wants the checked CRT can ask for it on its own.
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)

# ---------------------------------------------------------------------------
# basalt_win32_view — the Win32 view layer.
#
# No React Native dependency, so it builds and is tested on its own, on a
# Windows box with nothing installed but a compiler and the SDK. Direct2D, WIC
# and USER32 all ship with the operating system; there is nothing to fetch.
# ---------------------------------------------------------------------------
add_library(basalt_win32_view STATIC
  win32/RnWin32View.cpp
  # The paragraph object. DirectWrite only, no React Native, which is what keeps
  # this target buildable and testable with nothing else installed.
  win32/RnWin32TextLayout.cpp
  # <Image> pixels: the WIC decode and the four resize modes. The fetch that
  # feeds it is core/ImageBytes.cpp in the shared half, because a URI means the
  # same thing on every desktop.
  win32/RnWin32Image.cpp
  # UI Automation. Windows inverts what the other two do -- a provider answers
  # questions rather than a widget carrying properties; see the header.
  win32/RnWin32Accessible.cpp
  # Offscreen rendering, to a PNG or to pixels a test can read. The second is
  # why this is in the view target rather than beside the demo: it is what the
  # rendering assertions in tests/test_win32_paint.cpp are built on.
  win32/Win32Snapshot.cpp
  # Where a hidden title bar's pieces are -- the caption buttons and the drag
  # regions. Pure geometry over the view tree, so it is tested here with no
  # window and no React Native.
  win32/Win32TitleBarLayout.cpp)
target_compile_options(basalt_win32_view PRIVATE ${BASALT_WIN32_WARNINGS})
target_include_directories(basalt_win32_view PUBLIC win32)
# core/FocusRing.h and core/Backface.h, and nothing else from there. Both are
# header-only and have no React Native in them, so taking them does not make
# this target depend on the half that does -- and the alternative is a focus
# ring that is a different blue on each desktop, and three toolkits each with
# their own idea of which way a rotated card is facing.
#
# Header-only is the constraint, not a preference: this library links no core,
# so anything it takes from there has to be `inline`.
target_include_directories(basalt_win32_view PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../react-native-basalt/native/core)
target_link_libraries(basalt_win32_view PUBLIC
  d2d1 dwrite windowscodecs uiautomationcore ole32 oleaut32 user32 shcore)

add_executable(demo_layout_win32 win32/demo_layout_win32.cpp)
target_compile_options(demo_layout_win32 PRIVATE ${BASALT_WIN32_WARNINGS})
target_link_libraries(demo_layout_win32 PRIVATE basalt_win32_view)

# The runner comes from the shared half, which knows about no toolkit: the test
# infrastructure is shared even though the views are not. Unlike the GTK suite
# this one brings no separate entry point -- there is nothing to initialise
# before a view exists -- so `main` sits at the bottom of test_win32_view.cpp,
# as it does on the AppKit side.
set(BASALT_WIN32_TEST_SOURCES
  ${BASALT_TEST_HARNESS}
  tests/test_win32_view.cpp
  tests/test_win32_key_events.cpp
  # The mapping under test; this host is the one nobody can compile on a Mac, so
  # CI is the only thing that checks its table.
  win32/Win32KeyEvents.cpp
  # What actually reached the pixels. Needs no display and no window, which is
  # why this platform can assert it and the other two cannot; see the file.
  tests/test_win32_paint.cpp
  # Picking. In the view layer rather than beside a touch dispatcher, so that
  # testing it needs no React Native -- the same arrangement AppKit settled on.
  tests/test_win32_hittest.cpp
  # DirectWrite measurement, against RnWin32TextLayout alone.
  tests/test_win32_text.cpp
  # The resize modes, against a real bitmap and real pixels.
  tests/test_win32_image.cpp
  # The UIA mapping. The React Native role name is what the cross-platform diff
  # compares; this is where the platform half is checked.
  tests/test_win32_accessibility.cpp
  # A hidden title bar's geometry: the caption buttons and the drag regions.
  tests/test_win32_titlebar.cpp)

# ---------------------------------------------------------------------------
# basalt_win32_mounting — the Windows half of the Fabric mounting manager.
#
# The mutation walk is not here: it is in the shared half, in
# core/MountingWalk.h. What is here is what is genuinely Windows about mounting,
# plus the four seams a platform owes the core.
#
# Skipped when the build was not pointed at a React Native, so the view layer
# above still builds and tests on a Windows box with nothing but a compiler.
# ---------------------------------------------------------------------------
if(TARGET basalt_core)
  add_library(basalt_win32_mounting STATIC
    win32/Win32MountingManager.cpp
    # Mouse messages -> React Native touches. In the library rather than in the
    # host so that tests can drive it, which is the only way the touch state
    # machine is provable without a cursor.
    win32/Win32Focus.cpp
    win32/Win32KeyEvents.cpp
    # Why Windows cannot show a notification; see the file.
    ${BASALT_PACKAGE_WIN32_SOURCES}
    # The AppUserModelID and the Start Menu shortcut behind it, which is what
    # makes a notification say which application it came from.
    win32/Win32Packaging.cpp
    win32/Win32TouchDispatcher.cpp
    # <ScrollView>: the offset, the unthrottled state write-back, the wheel and
    # the commands. Routing the wheel is this platform's alone -- there is no
    # responder chain and no per-widget controller to attach one to.
    win32/Win32ScrollView.cpp
    # <TextInput>: a real EDIT control, which is a child window rather than a
    # painted view -- so it is also the only component whose peer the host has
    # to position, forward messages to, and answer colour questions for.
    win32/Win32TextInput.cpp
    # <Image> pixels: the fetch and the decode, both off the UI thread. The
    # fetch itself is core/ImageBytes.cpp in the shared half.
    win32/Win32ImageLoader.cpp
    # Which components this platform claims. See core/ComponentRegistry.h, and
    # note this and Win32MountingManager::hasComponent must agree.
    win32/ComponentRegistryWin32.cpp
    # AttributedString -> RnWin32TextLayout. Shared by measurement and painting,
    # so they agree, which is the whole reason it is one file.
    win32/DirectWriteLayout.cpp
    # Replaces React Native's stub TextLayoutManager; see the shared half's
    # cmake/ReactNativeCore.cmake for why the stub has to leave the build.
    win32/DirectWriteLayoutManager.cpp
    # Windows' implementation of core/FontRegistry.h -- the seam phase 17 named.
    win32/FontRegistryDirectWrite.cpp
    # ...and of core/ColorScheme.h.
    win32/ColorSchemeWin32.cpp
    # ...and of core/PlatformServices.h, which is also where postToUiThread
    # lives -- the marshalling the mounting manager depends on.
    win32/PlatformServicesWin32.cpp
    # The window's title bar: DWM's colours and dark mode, and the caption the
    # host draws when an app hides the system's. And BasaltWindow, the module
    # JavaScript reaches it through.
    # Accepting a file or text dragged onto the window; see
    # core/DragAndDrop.h for the model the three hosts share.
    win32/Win32DropTarget.cpp
    win32/Win32TitleBar.cpp
    # A window's size, position and full-screen state; see core/WindowControl.h.
    win32/Win32WindowControl.cpp
    # The application menu; see core/MenuModel.h for why the three differ.
    win32/Win32MenuBar.cpp
    win32/Win32WindowModule.cpp)
  target_compile_options(basalt_win32_mounting PRIVATE ${BASALT_WIN32_WARNINGS})
  target_include_directories(basalt_win32_mounting PUBLIC win32)
  target_link_libraries(basalt_win32_mounting PUBLIC basalt_win32_view basalt_core)
  # shell32 for ShellExecute; comctl32 for SetWindowSubclass, which is how a
  # <TextInput>'s EDIT peer is subclassed without breaking anything else that
  # subclasses it later.
  # shell32 for Shell_NotifyIcon and SHGetKnownFolderPath, ole32 for the
  # IShellLink that writes the Start Menu shortcut, propsys for the property
  # store that puts the AppUserModelID on it.
  target_link_libraries(basalt_win32_mounting PUBLIC shell32 comctl32 ole32 propsys)
  # dwmapi for the title bar's colours and dark mode.
  target_link_libraries(basalt_win32_mounting PUBLIC dwmapi)

  # -------------------------------------------------------------------------
  # basalt_win32 — a ReactHost, Hermes, and one surface in an HWND.
  # -------------------------------------------------------------------------
  add_executable(basalt_win32
    win32/main_win32.cpp
    # The event beat. Not an observer on Windows: there is no "about to wait"
    # hook, so the message loop itself is here. See the header.
    win32/Win32RunLoopObserver.cpp
    win32/Win32AnimationChoreographer.cpp)
  target_compile_options(basalt_win32 PRIVATE ${BASALT_WIN32_WARNINGS})
  target_link_libraries(basalt_win32 PRIVATE basalt_win32_mounting)
  if(BASALT_PACKAGE_WIN32_LINK_LIBRARIES)
    target_link_libraries(basalt_win32 PRIVATE ${BASALT_PACKAGE_WIN32_LINK_LIBRARIES})
  endif()

  # Real mutations into real views, with no JavaScript anywhere. The checkpoint
  # between "the mounting manager compiles" and "React Native runs": a mistake
  # in the Create/Insert/Update/Remove/Delete handling shows up in a picture
  # rather than in review.
  add_executable(mount_harness_win32 win32/mount_harness_win32.cpp)
  target_compile_options(mount_harness_win32 PRIVATE ${BASALT_WIN32_WARNINGS})
  target_link_libraries(mount_harness_win32 PRIVATE basalt_win32_mounting)

  list(APPEND BASALT_WIN32_TEST_SOURCES
    ${BASALT_CORE_TESTS}
    # Deliberately the same nine questions tests/test_mounting.cpp asks of GTK
    # and tests/test_appkit_mounting.mm asks of AppKit, in the same order with
    # the same tags. Most of what they test is whether the sharing still holds.
    tests/test_win32_mounting.cpp
    # The image loader: the cache, the failure paths, and the two lifetimes.
    # In the RN-dependent set because the fetch is core/ImageBytes.cpp.
    tests/test_win32_imageloader.cpp
    # The touch state machine and the gesture handover, asking what
    # tests/test_appkit_input.mm asks of AppKit.
    tests/test_win32_input.cpp
    # <ScrollView>: the clamp, the throttle, the state write-back, the wheel's
    # innermost-first routing and the commands.
    tests/test_win32_focus.cpp
    tests/test_win32_scrollview.cpp
    # <TextInput>. The only tests here that make a window, because a text
    # field's peer is one -- see the file.
    tests/test_win32_textinput.cpp
    # The clipboard, and what happens when another program has it open.
    tests/test_win32_clipboard.cpp
    # The caption actions with no window attached. Here rather than beside
    # test_win32_titlebar.cpp because Win32TitleBar is in basalt_win32_mounting,
    # which only exists when basalt_core does; its neighbour is pure geometry.
    tests/test_win32_titlebar_actions.cpp)
endif()

# ---------------------------------------------------------------------------
# Still to come:
#
#   mount_harness_win32     real ShadowViewMutations, no JavaScript, rendered
#                           to a PNG -- the checkpoint between "the mounting
#                           manager compiles" and "React Native runs".
#   basalt_win32            a ReactHost, Hermes, and one surface in an HWND.
#
# And the components this platform does not claim yet: <ScrollView>, which
# needs onScroll and scroll state, and <TextInput>, which needs an EDIT peer.
# ---------------------------------------------------------------------------

add_executable(basalt_win32_tests ${BASALT_WIN32_TEST_SOURCES})
target_compile_options(basalt_win32_tests PRIVATE ${BASALT_WIN32_WARNINGS})
target_include_directories(basalt_win32_tests PRIVATE ${BASALT_TEST_INCLUDE})
# basalt_win32_mounting already carries basalt_win32_view; naming both makes the
# linker warn about a duplicate library rather than doing anything useful.
if(TARGET basalt_win32_mounting)
  target_link_libraries(basalt_win32_tests PRIVATE basalt_win32_mounting)
  if(BASALT_PACKAGE_WIN32_LINK_LIBRARIES)
    target_link_libraries(basalt_win32_tests PRIVATE ${BASALT_PACKAGE_WIN32_LINK_LIBRARIES})
  endif()
else()
  target_link_libraries(basalt_win32_tests PRIVATE basalt_win32_view)
endif()
