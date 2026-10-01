// `globalThis.__turboModuleProxy`, which this platform does not otherwise have.
//
// `TurboModuleRegistry.requireModule` asks two things in order:
//
//     if (turboModuleProxy != null) { const m = turboModuleProxy(name); if (m) return m; }
//     const legacy = NativeModules[name]; if (legacy) return legacy;
//
// and `TurboModuleBinding::install` installs the first only when `RN$Bridgeless`
// is absent, and `nativeModuleProxy` -- which is what `NativeModules` reads --
// otherwise. This platform is bridgeless, so it gets the second and every lookup
// arrives by the fallback. On the React Native this is built against that is
// fine: the fallback is unconditional.
//
// It is not fine on 0.82 and earlier, where the fallback is gated behind
// `RN$Bridgeless !== true || RN$TurboInterop === true ||
// RN$UnifiedNativeModuleProxy === true`, all of which are false here. So on those
// versions every `getEnforcing` fails, starting with `PlatformConstants`, and an
// app has to prepend a flag to its own bundle to start at all. React Native's
// own modules are reached the same way, which is why this is not a niche
// problem on those versions -- it is all of them.
//
// This installs the first question as an alias for the second, so path one
// succeeds and the gate is never reached:
//
//     __turboModuleProxy = name => nativeModuleProxy[name]
//
// **An alias rather than a second provider**, which is the point. Writing our own
// would see only the modules this platform registers and not React Native's, so
// it would answer null for `PlatformConstants` -- and on the very versions this
// exists for, a null from path one does not fall through. Delegating means it
// answers for exactly what the platform can answer for, whoever provides it.
//
// Read lazily, at call time, so it does not matter whether `nativeModuleProxy`
// exists yet when this runs. It does not replace an existing
// `__turboModuleProxy`: a non-bridgeless runtime has a real one, and this is not
// better than it.

#pragma once

#include <jsi/jsi.h>

namespace basalt {

void installTurboModuleProxy(facebook::jsi::Runtime &runtime);

} // namespace basalt
