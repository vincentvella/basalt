# Provides the third-party CMake target *names* React Native's own CMakeLists
# link against, backed by system packages where possible and by vendored
# sources where not.
#
# React Native normally gets these from its Android build (gradle downloads and
# unpacks each one under third-party-ndk). A host build has no such step, so we
# supply equivalents here. Fantom does the same thing for its tester.
#
# Two ways of finding the same four libraries, and the split is Windows.
#
# On Linux and macOS they come from the system -- apt or Homebrew -- and
# `find_library` is the right tool, because a distro's package installs a bare
# `libglog.so` in a directory the linker already searches. On Windows there is
# no system to install them into. vcpkg is the equivalent, and it ships proper
# CMake config packages, which is a better answer than `find_library` even
# where both would work: a config package carries the include directories, the
# transitive dependencies and the debug/release split, and a bare
# `find_library` on `boost_regex` would not even match the name vcpkg gives it
# (`boost_regex-vc143-mt-x64-1_89.lib`).
#
# The target names below -- `glog`, `boost`, `double-conversion`, `fmt` -- are
# not ours to choose either way. They are what React Native's own CMakeLists
# link against.
if(WIN32)
  find_package(glog CONFIG REQUIRED)
  find_package(Boost CONFIG REQUIRED COMPONENTS regex thread)
  find_package(double-conversion CONFIG REQUIRED)
  find_package(fmt CONFIG REQUIRED)

  # React Native's CMakeLists link the bare names; vcpkg's config packages
  # define namespaced ones. These INTERFACE targets stand in the middle.
  # Guarded on the name not already existing, because a config package is
  # entitled to define the bare name too and CMake refuses a duplicate.
  if(NOT TARGET glog)
    add_library(glog INTERFACE)
    target_link_libraries(glog INTERFACE glog::glog)
  endif()
  target_compile_definitions(glog INTERFACE GLOG_USE_GLOG_EXPORT)

  add_library(boost INTERFACE)
  # Boost::thread as well as regex: folly's Windows PThread.cpp is written over
  # boost::thread, and its thread-local storage needs the compiled library
  # rather than the headers.
  target_link_libraries(boost INTERFACE Boost::regex Boost::thread Boost::headers)

  if(NOT TARGET double-conversion)
    add_library(double-conversion INTERFACE)
    target_link_libraries(double-conversion INTERFACE double-conversion::double-conversion)
  endif()

  if(NOT TARGET fmt)
    add_library(fmt INTERFACE)
    target_link_libraries(fmt INTERFACE fmt::fmt)
  endif()

  # As on the other platforms: nothing on a host build needs glog's signal
  # handlers, and React Native's Android build is the only thing that has them.
  add_library(glog_init INTERFACE)
else()

# --- glog ------------------------------------------------------------------
# RN pins glog 0.3.5; distros ship 0.7.x, which added a guard requiring
# GLOG_USE_GLOG_EXPORT when the headers are not consumed via glog's own CMake
# target. Without it every glog header hard-errors.
find_library(GLOG_LIBRARY glog REQUIRED)
find_path(GLOG_INCLUDE_DIR glog/logging.h REQUIRED)
add_library(glog INTERFACE)
target_link_libraries(glog INTERFACE ${GLOG_LIBRARY})
# Distros put headers in /usr/include; Homebrew does not, so carry the path.
target_include_directories(glog INTERFACE ${GLOG_INCLUDE_DIR})
target_compile_definitions(glog INTERFACE GLOG_USE_GLOG_EXPORT)

# RN's Android build has a glog_init target that installs signal handlers.
# Nothing on a host build needs it; Fantom stubs it the same way.
add_library(glog_init INTERFACE)

# --- boost -----------------------------------------------------------------
# Headers only, with one exception: folly's Uri parser uses boost::regex, which
# is a compiled library. Uri is pulled in by React Native's websocket client
# (see rn_websocket in ReactNativeCore.cmake), so it is needed as soon as the
# dev server is. boost::asio and boost::beast, which that client also uses, are
# header-only.
find_path(BOOST_INCLUDE_DIR boost/version.hpp REQUIRED)
find_library(BOOST_REGEX_LIBRARY NAMES boost_regex boost_regex-mt REQUIRED)
add_library(boost INTERFACE)
target_include_directories(boost INTERFACE ${BOOST_INCLUDE_DIR})
target_link_libraries(boost INTERFACE ${BOOST_REGEX_LIBRARY})

# --- double-conversion -----------------------------------------------------
find_library(DOUBLE_CONVERSION_LIBRARY double-conversion REQUIRED)
find_path(DOUBLE_CONVERSION_INCLUDE_DIR double-conversion/double-conversion.h REQUIRED)
add_library(double-conversion INTERFACE)
target_link_libraries(double-conversion INTERFACE ${DOUBLE_CONVERSION_LIBRARY})
target_include_directories(double-conversion INTERFACE ${DOUBLE_CONVERSION_INCLUDE_DIR})

# --- fmt -------------------------------------------------------------------
find_library(FMT_LIBRARY fmt REQUIRED)
find_path(FMT_INCLUDE_DIR fmt/format.h REQUIRED)
add_library(fmt INTERFACE)
target_link_libraries(fmt INTERFACE ${FMT_LIBRARY})
target_include_directories(fmt INTERFACE ${FMT_INCLUDE_DIR})

endif() # WIN32 -- everything below is the same on all three platforms.

# --- fast_float (vendored, header-only) ------------------------------------
add_library(fast_float INTERFACE)
target_include_directories(fast_float INTERFACE ${FAST_FLOAT_DIR}/include)

# --- folly_runtime ---------------------------------------------------------
# The trimmed folly RN actually builds: ~30 translation units, no config step,
# no coroutines. Source list and flags mirror
# private/react-native-fantom/tester/third-party/folly/CMakeLists.txt.
# Three of these are claims about POSIX and are false on Windows. folly reads
# them as "the platform has this", not as "please provide it", so leaving them
# defined on Windows makes it call recvmmsg, pthread_setname_np and the XSI
# strerror_r, none of which exist -- and it fails at link rather than at
# configure, which is a worse place to find out.
#
# FOLLY_HAVE_CLOCK_GETTIME is the interesting one. Windows has no
# clock_gettime, but folly's portability layer *provides* one, and defining the
# flag is how folly is told to use its own rather than expect the system's.
set(folly_FLAGS
        -DFOLLY_NO_CONFIG=1
        -DFOLLY_HAVE_CLOCK_GETTIME=1
        -DFOLLY_CFG_NO_COROUTINES=1
        -DFOLLY_MOBILE=0)
if(NOT WIN32)
  list(APPEND folly_FLAGS
        -DFOLLY_HAVE_RECVMMSG=1
        -DFOLLY_HAVE_PTHREAD=1
        -DFOLLY_HAVE_XSI_STRERROR_R=1)
endif()

set(folly_runtime_SRC
        Conv.cpp Demangle.cpp FileUtil.cpp Format.cpp ScopeGuard.cpp
        SharedMutex.cpp String.cpp Unicode.cpp
        concurrency/CacheLocality.cpp
        container/detail/F14Table.cpp
        detail/FileUtilDetail.cpp detail/Futex.cpp
        detail/SplitStringSimd.cpp detail/UniqueInstance.cpp
        hash/SpookyHashV2.cpp
        io/IOBuf.cpp
        json/dynamic.cpp json/json_pointer.cpp json/json.cpp
        # Not in React Native's own folly source list. Its websocket client
        # parses the packager URL with folly::Uri, so a host that talks to the
        # dev server needs it.
        Uri.cpp
        lang/CString.cpp lang/Exception.cpp lang/SafeAssert.cpp lang/ToAscii.cpp
        memory/SanitizeAddress.cpp memory/detail/MallocImpl.cpp
        net/NetOps.cpp
        portability/SysUio.cpp
        synchronization/SanitizeThread.cpp synchronization/ParkingLot.cpp
        system/AtFork.cpp system/ThreadId.cpp system/ThreadName.cpp)
# folly's Windows portability layer, which React Native's own source list does
# not carry because React Native does not build folly on Windows -- it consumes
# a prebuilt one. These are not shims this project wrote: nineteen of folly's
# twenty-one portability sources already have `_WIN32` branches, and this is the
# subset the translation units above reach. Without them the build gets all the
# way to the linker and fails on `open`, `pthread_key_create`, `gettimeofday`
# and thirty more.
if(WIN32)
  list(APPEND folly_runtime_SRC
        portability/Dirent.cpp
        portability/Fcntl.cpp
        portability/Malloc.cpp
        portability/PThread.cpp
        portability/Sched.cpp
        portability/Sockets.cpp
        portability/Stdio.cpp
        portability/Stdlib.cpp
        portability/String.cpp
        portability/SysFile.cpp
        portability/SysMembarrier.cpp
        portability/SysMman.cpp
        portability/SysResource.cpp
        portability/SysStat.cpp
        portability/SysTime.cpp
        portability/Time.cpp
        portability/Unistd.cpp
        # Not under portability/: folly keeps its Windows socket-handle table in
        # net/detail. Without it the link fails on SocketFileDescriptorMap, which
        # is what folly::netops uses to make a SOCKET look like an fd.
        net/detail/SocketFileDescriptorMap.cpp)
endif()

list(TRANSFORM folly_runtime_SRC PREPEND ${FOLLY_DIR}/folly/)

add_library(folly_runtime STATIC ${folly_runtime_SRC})
target_compile_options(folly_runtime PRIVATE
        ${BASALT_EXCEPTIONS_FLAG} ${BASALT_FRAME_POINTER_FLAG} ${BASALT_RTTI_FLAG}
        ${BASALT_NO_SIGN_COMPARE_FLAG} ${folly_FLAGS})
target_compile_options(folly_runtime PUBLIC ${folly_FLAGS})
if(WIN32)
  # ws2_32 for the sockets folly's net layer wraps, and NOMINMAX because folly
  # uses std::min and std::max in headers that windows.h has already redefined
  # as macros by the time they are reached.
  # winmm for timeBeginPeriod/timeEndPeriod, which folly's Windows clock uses to
  # ask for a finer timer resolution.
  target_link_libraries(folly_runtime ws2_32 winmm)
  target_compile_definitions(folly_runtime PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
endif()
target_include_directories(folly_runtime PUBLIC ${FOLLY_DIR})
target_link_libraries(folly_runtime glog double-conversion boost fmt fast_float)

# --- nlohmann_json (vendored, header-only) ---------------------------------
# Needed by ReactCxxPlatform's devsupport (PackagerConnection).
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${NLOHMANN_JSON_DIR}/include)

# --- OpenSSL ---------------------------------------------------------------
# Also devsupport: the packager connection speaks WebSocket over TLS.
find_package(OpenSSL REQUIRED)

# --- libcurl ---------------------------------------------------------------
# Backs src/LinuxNetworking.cpp's IHttpClient: the dev server bundle fetch and
# NetworkingModule (fetch/XHR) both go through it. Present on macOS in the SDK;
# on Linux it is the curl development package.
find_package(CURL REQUIRED)

# --- Hermes ----------------------------------------------------------------
# Built separately from RN's pinned tarball; see README. Fantom locates the
# same library out of its Android hermes-engine build dir.
# Two names, because Hermes renamed the library. React Native 0.81's Hermes
# builds `libhermes`; the one 0.87 and main use builds `libhermesvm`. Which one
# is present follows from the version in supported-versions.json, so this looks
# for either rather than being told.
find_library(HERMES_VM_LIBRARY NAMES hermesvm hermes
  HINTS ${HERMES_BUILD_DIR}/API/hermes ${HERMES_BUILD_DIR}/lib
  REQUIRED)

# Two things about Hermes on Windows that follow from static archives not
# behaving like shared libraries, and neither of which is visible elsewhere.
#
# First, the name found above is wrong here. Hermes' `hermesvm` target builds a
# DLL, and a Windows DLL exports nothing without __declspec(dllexport), which
# Hermes does not annotate -- so `hermesvm.lib` is a one-kilobyte import library
# for a DLL with nothing in it, next to a thirty-megabyte `hermesvm_a.lib` that
# has the engine. On Linux the shared object works because ELF exports
# everything by default; that default is the only reason the name is right
# there.
#
# Second, jsi has to be named separately. `-DJSI_DIR` makes the Hermes build
# compile React Native's jsi, and on Linux libhermesvm.so absorbs it the way a
# shared library absorbs its static dependencies. A static archive absorbs
# nothing, so on Windows jsi.lib is a second file and the link fails on
# jsi::Value and jsi::JSError without it.
if(WIN32)
  find_library(HERMES_VM_STATIC_LIBRARY NAMES hermesvm_a
    HINTS ${HERMES_BUILD_DIR}/lib REQUIRED)
  find_library(HERMES_JSI_LIBRARY NAMES jsi
    HINTS ${HERMES_BUILD_DIR}/jsi REQUIRED)
  # And ICU. Hermes reports "Using Windows 10 built-in ICU" at configure time
  # and defines USE_WIN10_ICU, which means it calls u_strToUpper, ucol_strcoll
  # and friends out of the operating system's own icu.dll rather than vendoring
  # a copy. `icu.lib` is that DLL's import library and ships with the Windows
  # SDK. Nothing names it for us: on Linux the equivalent arrives through
  # pkg-config, and on a shared-library platform it would have been recorded as
  # a dependency of libhermesvm.so.
  set(HERMES_LINK_LIBRARIES
    ${HERMES_VM_STATIC_LIBRARY} ${HERMES_JSI_LIBRARY} icu)
else()
  set(HERMES_LINK_LIBRARIES ${HERMES_VM_LIBRARY})
endif()
# Both names, for the same reason the find_library above takes both: React
# Native 0.81's own CMakeLists link hermes-engine::libhermes, and 0.87 and main
# link hermes-engine::hermesvm. Only one of them is ever referenced by a given
# React Native, and defining the other costs nothing.
# GLOBAL because the platform packages are separate directories now: an
# imported target is visible only in the directory that created it and below,
# and basalt-gtk adds this one as a *sibling* subdirectory rather
# than containing it. Without GLOBAL the failure is "target was not found" at
# generate time, naming the target and not the scope.
foreach(hermes_alias hermesvm libhermes)
  add_library(hermes-engine::${hermes_alias} INTERFACE IMPORTED GLOBAL)
  # Quoted, because on Windows this is two libraries rather than one and an
  # unquoted list would arrive as extra arguments to set_target_properties.
  set_target_properties(hermes-engine::${hermes_alias} PROPERTIES
    INTERFACE_LINK_LIBRARIES "${HERMES_LINK_LIBRARIES}")
endforeach()

# Hermes' public headers must be globally visible: RN's own hermes/executor and
# hermes/inspector-modern targets include <hermes/hermes.h> without declaring a
# dependency that would carry the path. Fantom does the same with a global
# include_directories() over its prefab-headers dir.
#
# RN's jsi is used rather than Hermes' vendored copy, so API/ and public/ only.
include_directories(
  ${HERMES_DIR}/API
  ${HERMES_DIR}/public
  ${HERMES_BUILD_DIR}/lib/config)
