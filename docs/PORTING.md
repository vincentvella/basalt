# Porting notes

What it takes to build React Native's C++ core somewhere it was not built
before, and the handful of upstream facts that the build scripts depend on.

This is the file `native/bootstrap.sh`, `scripts/react-native.pin` and
`.github/workflows/ci.yml` point at. Everything here was arrived at by building
rather than by reading, and each section says what breaks if you do not do it.

## Building against an installed React Native

`ReactCxxPlatform` is not in the npm package.
`packages/react-native/package.json` lists `ReactCommon`, `ReactAndroid` and
`ReactApple` under `files`, and not `ReactCxxPlatform`, so an installed React
Native does not carry the layer an out-of-tree C++ platform is built on, and
`react-native run-linux --build` used to need a source checkout. Every app has
an installed React Native and almost none has a checkout.

**The upstream fix is one line**, adding `"ReactCxxPlatform"` to that `files`
array. It costs about 1% of the package. Nothing in the existing issues and pull
requests mentioning `ReactCxxPlatform` raises the packaging question, and
nothing has been opened from here: a one-line packaging change with one obscure
consumer is easy to ignore, and this platform has no users yet to point at.

**The workaround is to fetch the directory, not to vendor it.** When React
Native is an installed package with no `ReactCxxPlatform`, bootstrap does a
sparse, blobless clone of that one directory at the tag matching the app's exact
version: about 3MB and four seconds, a smaller bargain than the folly and
Hermes downloads it already makes.

Fetching is forced rather than preferred. Carrying a copy in this package was
the obvious answer and cannot work: that layer tracks `ReactCommon` closely, and
`main`'s copy fails against 0.87.1 on a ResizeObserver header that did not exist
and a `ReactInstance::createJSCallInvoker` that had not been added. One copy
cannot serve several React Natives, and a vendored copy would pin this package
to a single version the way react-native-windows pins to one exact nightly.
Fetching at the app's own version keeps the C++ and the JavaScript in step by
construction.

**A second, smaller gap**, which the build found and a path check did not:
`ReactCommon/react/nativemodule/cputime` ships its C++ but not its codegen spec,
which lives under `src/private/testing/fantom/specs/` and is reasonably
excluded. The package therefore contains C++ that cannot be compiled from the
package, and the failure reads as a missing `NativeCPUTimeCxxSpec` template,
like a codegen bug rather than a packaging one. Nothing links that target, so
this platform does not build it.

## Which React Natives are supported

`supported-versions.json` names, for each supported React Native, the Hermes to
build. Bootstrap reads it before downloading or compiling anything and refuses
an unsupported version in seconds, with a message saying what is supported and
where to look.

Today that is React Native 0.87.x, verified against 0.87.1, and `main` for
development.

A pinned triple rather than following the app, because following the app meant
every combination was its own experiment. Adding a version is now a deliberate
act: build against it, run both suites, add an entry. It cannot be done by
widening a range, and that is the point; 0.83 through 0.86 were once described
as "expected to work", which was a prediction wearing the clothes of support.
They are listed as untested and refused.

`ReactCxxPlatform` still comes from the app's exact version rather than from the
table, and that is not an inconsistency: the table bounds *which* minors are
supported, and the fetch keeps the C++ and the JavaScript exact *within* one.

## Hermes on Windows

Windows is listed among the platforms Meta publishes a prebuilt Hermes for, and
the prebuilt does not exist. So Windows builds Hermes from source, which needs
three things, one of them an upstream bug.

**`HERMES_ALLOW_BOOST_CONTEXT=0`.** Hermes' vendored `boost::context` throws
`std::bad_alloc` from its *Windows* stack allocator, and Hermes compiles with
exceptions off. The POSIX allocator does not, which is why this is Windows-only.
This is Hermes' own option for exactly that case, the one its ASAN and
Emscripten builds use, and the whole path is behind `#if
HERMES_USE_BOOST_CONTEXT`, so it is a clean fallback rather than a hole.

**`HERMES_EMPTY_BASES` on `VM::Environment`.** Static Hermes asserts that the
C++ `Environment` and the C `SHEnvironment` are layout-identical. `sizeof`
matches and the offsets do not, because `Environment` multiply-inherits from
`VariableSizeRuntimeCell` and an *empty* base, `llvh::TrailingObjects`, and the
MSVC ABI does not collapse empty bases the way the Itanium ABI does.

The fix is one word and Hermes already has it. `Support/Compiler.h` defines

    #define HERMES_EMPTY_BASES __declspec(empty_bases)

with a comment saying it is necessary for `PointerBase` alignment under
`HERMESVM_CONTIGUOUS_HEAP`, which is the mode this builds in, and the macro is
applied to nothing at all in the entire tree. Dead code that was presumably
attached to something once. Applying it to `Environment` clears the assertion.

**Exceptions, for React Native's jsi.** A POSIX assumption hiding inside a
difference between two compilers' defaults, and the most interesting of the
three.

`jsi.cpp` throws `JSError`. Hermes' `cmake/modules/Hermes.cmake` strips `/EHsc`
out of `CMAKE_CXX_FLAGS` wholesale to avoid a D9025 warning, then puts `/EHsc`
or `/EHs-c-` back on each of *its own* targets through
`hermes_update_compile_flags()`. React Native's `jsi` is not one of its own
targets: it arrives through `JSI_DIR`, never goes through that helper, and so
ends up with **no `/EH` flag at all**, CMake's default having been removed on
its behalf. clang with no flag defaults exceptions *on*; clang-cl with no flag
defaults them *off*. The strip is safe on every platform Hermes is tested on and
fatal on this one.

`HERMES_ENABLE_EH=ON` is the supported knob for Hermes' own targets, and it has
to be `HERMES_ENABLE_RTTI=ON` alongside it, `API/hermes/CMakeLists.txt` refuses
the mixed case outright. Worth knowing because the failure arrives as a
configure error about ICU much further down the log, which is collateral and
sends you looking in the wrong place.

For the jsi target the flag has to come from `CMAKE_CXX_FLAGS` instead, spelled
`/EHs /EHc`: exactly equivalent, and not the literal string their `REPLACE`
looks for.

One trap, recorded because it cost a full rebuild: `-DCMAKE_CXX_FLAGS=...`
**replaces** CMake's platform defaults rather than adding to them, and those
defaults are where `/DWIN32 /D_WINDOWS` come from. Hermes' `OSCompat.h` keys its
`<unistd.h>` include on `_WINDOWS`, so overriding the variable makes a Windows
build start looking for POSIX headers. Pass the defaults back with it.

## clang-cl rather than cl

The two front ends fail differently on Hermes, and the difference decided the
toolchain for the whole core build:

- **cl** stops at 260 of 415 on `__builtin_expect` in Static Hermes'
  `static_h.h`: a GCC and Clang builtin MSVC does not have, and not something a
  flag can supply.
- **clang-cl** has that builtin and gets past it, which is what makes the three
  fixes above sufficient rather than the first of a long list.

`docs/DECISIONS.md` also prefers clang wherever there is a choice, for
independent reasons.

## Upstream portability bugs this build works around

Both are the same shape (a POSIX assumption in a header) and both are fixed
from the command line rather than by editing React Native's tree:

- **`M_PI`** is used seven times in
  `react/renderer/components/view/conversions.h`, to convert degrees for
  `transform: rotate`. It is a POSIX extension rather than standard C++, and
  MSVC's `<cmath>` defines it only behind `_USE_MATH_DEFINES`.
- **A missing `<cstdint>`** in `HttpUtils.h`, and another in
  `ReactCxxPlatform/react/runtime/ReactInstanceConfig.h`, which this build
  force-includes around. The second appears only at React Native 0.86 on
  Linux: 0.87's copy of the header also includes `<functional>` and
  `<memory>`, which pull `<cstdint>` in on libstdc++, and libc++ supplies it
  on a Mac regardless.

`docs/backlog/upstream.md` carries these and the rest.
