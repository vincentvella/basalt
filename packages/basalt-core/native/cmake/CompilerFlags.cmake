# The same intentions, spelled for two compiler front ends.
#
# Everything in this build was written for clang's command line, because until
# Windows there were only clang targets: `docs/DECISIONS.md` records why the
# project is a clang codebase rather than a GCC one, and both desktops it had
# were clang. MSVC's front end takes none of those spellings, and clang-cl takes
# MSVC's rather than clang's -- it is the same compiler behind a different
# command line, which is exactly the trap: `-frtti` on clang-cl is silently
# interpreted as an input filename, not as a request for RTTI.
#
# So each flag this build actually depends on is named here once, and the two
# spellings sit side by side where a reader can check they mean the same thing.
# The alternative -- `if(MSVC)` scattered through ThirdParty.cmake and
# ReactNativeCore.cmake -- would put the two halves of each decision in
# different files.
#
# Note the guard is `MSVC`, not the compiler id: CMake sets MSVC for clang-cl
# too, which is what we want, because the command line is what differs.

if(MSVC)
  # /EHsc: C++ exceptions, and extern "C" assumed not to throw. React Native
  # and folly both throw across their own boundaries, so this is not optional.
  set(BASALT_EXCEPTIONS_FLAG /EHsc)
  # /GR is MSVC's default, stated anyway because folly's is not a default it
  # can afford to have changed underneath it -- folly::dynamic is built on
  # dynamic_cast.
  set(BASALT_RTTI_FLAG /GR)
  set(BASALT_FRAME_POINTER_FLAG /Oy-)
  # C4018 and C4389 are the signed/unsigned comparison warnings folly's own
  # build turns off with -Wno-sign-compare.
  set(BASALT_NO_SIGN_COMPARE_FLAG /wd4018 /wd4389)
  # C4068 is "unknown pragma", which is `#pragma mark` in React Native's Apple
  # heritage.
  set(BASALT_NO_UNKNOWN_PRAGMAS_FLAG /wd4068)
  # /FI is MSVC's -include, and clang-cl takes it too. See the upstream
  # portability bug in ReactNativeCore.cmake for why this is force-included.
  set(BASALT_FORCE_INCLUDE_CSTDINT_FLAG /FIcstdint)
  # C4996: the CRT's "this function is unsafe" campaign, which fires on most of
  # POSIX. React Native and folly are not going to stop calling strncpy.
  set(BASALT_NO_DEPRECATED_FLAG /wd4996)
else()
  set(BASALT_EXCEPTIONS_FLAG -fexceptions)
  set(BASALT_RTTI_FLAG -frtti)
  set(BASALT_FRAME_POINTER_FLAG -fno-omit-frame-pointer)
  set(BASALT_NO_SIGN_COMPARE_FLAG -Wno-sign-compare)
  set(BASALT_NO_UNKNOWN_PRAGMAS_FLAG -Wno-unknown-pragmas)
  set(BASALT_FORCE_INCLUDE_CSTDINT_FLAG -include cstdint)
  set(BASALT_NO_DEPRECATED_FLAG -Wno-deprecated-declarations)
endif()
