/**
 * The type of this platform's `Platform`.
 *
 * Hand-written rather than compiled, because the implementations beside it are
 * not: `Platform.linux.js`, `Platform.macos.js` and `Platform.windows.js` are
 * chosen by Metro's platform extensions, and `createPlatform.js` imports React
 * Native internals through a self-importing shim. None of that is something a
 * type checker can follow, and none of it needs to be -- what a consumer needs
 * is this shape, which is React Native's own `Platform` with the desktops in
 * the OS union.
 *
 * The extensionless import in `../index.ts` resolves to this file for types and
 * to the right platform's file at bundle time. See metro-config.js.
 *
 * @format
 */

/** The three this platform builds for. */
export type DesktopOS = 'linux' | 'macos' | 'windows';

export type PlatformConstants = {
  Version: number;
  isTesting: boolean;
  isDisableAnimations?: boolean;
  [key: string]: unknown;
};

/**
 * `desktop` is deliberately absent as a `select` key. Adding it would make
 * shared code behave differently under React Native's own `Platform.select` on
 * iOS, which is exactly the divergence this project exists to avoid -- see
 * createPlatform.js.
 */
export type PlatformSelectSpec<T> = Partial<Record<DesktopOS, T>> & {
  native?: T;
  default?: T;
};

export type DesktopPlatform = {
  /**
   * The cached `getConstants()` answer. Part of the shape because the getters
   * below read it through `this`, which is how React Native's own Platform is
   * written too; not something an app should touch.
   */
  __constants: PlatformConstants | null;
  readonly OS: DesktopOS;
  readonly Version: number;
  readonly constants: PlatformConstants;
  readonly isTesting: boolean;
  readonly isDisableAnimations: boolean;
  /** Not a thing on a desktop, and read unconditionally by React Native. */
  readonly isTV: false;
  readonly isVision: false;
  select<T>(spec: PlatformSelectSpec<T>): T | undefined;
};

declare const Platform: DesktopPlatform;
export default Platform;
