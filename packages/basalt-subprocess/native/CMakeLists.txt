# The subprocess capability, contributed to whichever host is being built.
#
# A package does not build a library of its own: the host is one binary and
# everything native is compiled into it. So this appends to the global properties
# the core and host CMakeLists read -- see core/PackageModules.h for why the
# registration is generated rather than resolved at static initialisation.

set(BASALT_SUBPROCESS_DIR ${CMAKE_CURRENT_LIST_DIR})

# Platform-independent: the JavaScript boundary, compiled into basalt_core.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_CORE_SOURCES
  ${BASALT_SUBPROCESS_DIR}/SubprocessModule.cpp)

# The header the seam lives in, so core and the hosts can include it.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_INCLUDES ${BASALT_SUBPROCESS_DIR})

# The factory the generated translation unit offers by name. Unlike the
# notifications package this contributes a TurboModule rather than an Expo
# module: there is no Expo package for running a process, so there is no contract
# to match, and a TurboModule is reachable without Expo installed at all.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_TURBO_MODULES makeSubprocessTurboModule)

# What a platform would have to implement, stubbed, for the portability probe.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_PROBE_SOURCES
  ${BASALT_SUBPROCESS_DIR}/probe_stub.cpp)

# One implementation per desktop -- except that two of the three are the same
# one. fork, exec, pipe, poll and waitpid are POSIX, so macOS and Linux share a
# file; Windows has none of them and has its own.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_APPKIT_SOURCES
  ${BASALT_SUBPROCESS_DIR}/posix/SubprocessPosix.cpp)
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_GTK_SOURCES
  ${BASALT_SUBPROCESS_DIR}/posix/SubprocessPosix.cpp)
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_WIN32_SOURCES
  ${BASALT_SUBPROCESS_DIR}/win32/SubprocessWin32.cpp)

# And the tests, which run in every host's suite. That is the whole reason this
# capability is here rather than in the app that wanted it: the Windows half is
# compiled and exercised by this project's Windows CI, and an app's repository
# has nothing that would ever have built it.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_TESTS
  ${BASALT_SUBPROCESS_DIR}/tests/test_subprocess.cpp)
