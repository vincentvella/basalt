# Async storage, contributed to whichever host is being built.
#
# Two halves, and they are conditional on different things.
#
# The store is portable C++ over files and depends on nothing but nlohmann, so
# it is always compiled and always tested. That is deliberate: every rule worth
# getting wrong lives there -- what a database name may be, what merging means,
# what a half-written file leaves behind -- and a rule tested on one desktop
# and not the other two is how this project gets surprised.
#
# The module is the jsi half, and it derives from a base class that React
# Native's codegen writes out of async-storage's own TypeScript spec. That
# header exists only when the build ran codegen over an app that installed
# async-storage, which is what `appCodegen` in cli/desktop.ts does. An
# in-repository build has no app and no such header, so it compiles the store
# and nothing else, the way basalt-skia contributes nothing when the app has
# not installed Skia.

set(BASALT_ASYNC_STORAGE_DIR ${CMAKE_CURRENT_LIST_DIR})

# The store: always.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_CORE_SOURCES
  ${BASALT_ASYNC_STORAGE_DIR}/AsyncStorageStore.cpp)
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_INCLUDES ${BASALT_ASYNC_STORAGE_DIR})

# nlohmann, which the store parses and writes with. Vendored into third_party
# by bootstrap and already compiled against by core; named here as a system
# include because it is somebody else's header and is not held to this
# project's warnings.
if(DEFINED NLOHMANN_JSON_DIR)
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_SYSTEM_INCLUDES
    ${NLOHMANN_JSON_DIR}/include)
endif()

# And its tests, which run in every host's suite.
set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_TESTS
  ${BASALT_ASYNC_STORAGE_DIR}/tests/test_async_storage.cpp)

# The module: only where the spec was generated.
#
# Checked by looking for the header rather than by trusting the flag, because
# the two can disagree: an app whose dependencies declare a spec gets
# BASALT_APP_CODEGEN set, and async-storage may not be the module that
# declared one.
if(DEFINED BASALT_APP_CODEGEN AND
   EXISTS ${BASALT_APP_CODEGEN}/react/renderer/components/AsyncStorageSpec/AsyncStorageSpecJSI.h)
  message(STATUS "basalt-async-storage: the generated spec is here, building the module")
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_CORE_SOURCES
    ${BASALT_ASYNC_STORAGE_DIR}/AsyncStorageModule.cpp)
  set_property(GLOBAL APPEND PROPERTY BASALT_PACKAGE_TURBO_MODULES makeAsyncStorageTurboModule)
else()
  message(STATUS "basalt-async-storage: no generated spec, building the store only")
endif()
