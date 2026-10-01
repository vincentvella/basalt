/**
 * `ReactDevToolsSettingsManager`, for every desktop platform here.
 *
 * React Native ships this only as `.android.js` and `.ios.js`. There is no
 * platform-neutral fallback, and `Libraries/Core/setUpReactDevTools.js` imports
 * it unconditionally, so bundling for any other platform fails outright:
 *
 *     Unable to resolve module ../../src/private/devsupport/rndevtools/
 *     ReactDevToolsSettingsManager
 *
 * Only in a development bundle -- the production one never reaches
 * setUpReactDevTools, which is why this only appears with --dev true.
 *
 * Both real implementations forward to a `ReactDevToolsSettingsManager`
 * TurboModule, which ReactCxxPlatform does not provide: the host already logs
 * "Failed to load TurboModule: ReactDevToolsSettingsManager" and carries on.
 * So these are no-ops that keep the shape rather than stubs that throw. The
 * only thing lost is React DevTools remembering its settings between runs.
 *
 * @format
 */

'use strict';

export function setGlobalHookSettings(_settings: string) {}

export function getGlobalHookSettings() {
  return null;
}
