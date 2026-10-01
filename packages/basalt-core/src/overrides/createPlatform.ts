/**
 * `Platform`, for a desktop React Native platform.
 *
 * React Native ships `Platform.android.js` and `Platform.ios.js`, and a
 * `Platform.js` whose entire body is `import Platform from './Platform'` -- a
 * shim that exists so deep imports keep working, and which relies on a
 * platform-specific file winning the resolution. Bundle for a platform React
 * Native has never heard of and that import resolves to the shim itself, so
 * `Platform.constants` comes out undefined and the first thing to touch it
 * dies:
 *
 *     TypeError: Cannot read property 'isDisableAnimations' of undefined
 *
 * So a `Platform` has to exist for each of them, and `metro-config.js` is what
 * puts it in the shim's place.
 *
 * The only thing that differs between linux, macos and windows is the string.
 * Three copies of this file would be three chances for them to drift, and the
 * drift would be silent -- so there is one implementation and three two-line
 * modules beside it, which is also what makes `Platform.select` behave
 * identically on all three without anybody having to check.
 *
 * Constants come from `NativePlatformConstantsAndroid`. That is not a
 * placeholder: ReactCxxPlatform's PlatformConstantsModule genuinely returns
 * `PlatformConstantsAndroid`, so this is the spec the native side implements,
 * on every desktop.
 *
 * @format
 */

'use strict';

import NativePlatformConstants from 'basalt-core/upstream/Libraries/Utilities/NativePlatformConstantsAndroid';

import type {DesktopOS, DesktopPlatform, PlatformConstants} from './Platform';

export default function createPlatform(os: DesktopOS): DesktopPlatform {
  return {
    __constants: null as PlatformConstants | null,

    OS: os,

    get Version() {
      return this.constants.Version;
    },

    get constants(): PlatformConstants {
      // Assigned on the line above, which the compiler cannot see through a
      // getter reading `this`.
      if (this.__constants == null) {
        this.__constants = NativePlatformConstants.getConstants() as PlatformConstants;
      }
      return this.__constants;
    },

    get isTesting() {
      if (__DEV__) {
        return this.constants.isTesting;
      }
      return false;
    },

    get isDisableAnimations() {
      return this.constants.isDisableAnimations ?? this.isTesting;
    },

    // Neither is a thing on a desktop, and both are read unconditionally by
    // React Native's own code, so they answer rather than throw.
    get isTV(): false {
      return false;
    },

    get isVision(): false {
      return false;
    },

    // This platform first, then `native`, then `default` -- the same shape as
    // every other platform's select, so `Platform.select({native: ...,
    // default: ...})` keeps working in shared code.
    //
    // `desktop` is deliberately *not* a key here. It would be useful, and
    // adding it would mean shared code written against this project behaves
    // differently under React Native's own Platform.select on iOS -- which is
    // exactly the kind of divergence a project about consistency should not
    // introduce. If desktop-vs-mobile branching is wanted, it belongs in an
    // explicit helper rather than smuggled into an API React Native owns.
    select<T>(spec: Record<string, T> & {native?: T; default?: T}): T | undefined {
      return os in spec ? spec[os] : 'native' in spec ? spec.native : spec.default;
    },
  };
}
