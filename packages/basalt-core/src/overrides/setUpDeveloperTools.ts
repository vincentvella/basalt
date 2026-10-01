/**
 * `setUpDeveloperTools`, for a desktop React Native platform.
 *
 * React Native's own file, with one thing added: a listener that lets the host
 * drive the developer menu.
 *
 * Of the three things that menu does, two need nothing from here.
 * `toggleElementInspector` is React Native's own device event and
 * `AppContainer-dev.js` already listens for it; `openDebugger` is a method on
 * `ReactHost`. Reload is the odd one out, and the reason this override exists:
 * it lives on `ReactHost::reloadReactInstance`, which is private, and the only
 * thing wired to it is the callback React Native's `DevSettings` module holds.
 * So the host asks JavaScript to make the call, and this is what hears it.
 *
 * Why here rather than in a module an app imports: this file runs from
 * `InitializeCore` in every development bundle, before any application code,
 * and an app that imports nothing from `basalt-core` still gets a
 * working dev menu. That is also why it is a copy of upstream plus a listener
 * rather than something of our own -- the file has to keep doing its own job.
 *
 * The upstream half is deliberately unmodified. When it changes, this should be
 * recopied rather than merged: everything below the marker is ours and
 * everything above it is React Native's.
 *
 * @format
 */

import Platform from 'basalt-core/upstream/Libraries/Utilities/Platform';

// React Native installs its own console methods and reads a private flag off
// them. Declaring the shape here is what lets this file touch both without
// claiming to know the rest of the global console.
declare const console: Record<string, any> & {_isPolyfilled?: boolean};

/**
 * Sets up developer tools for React Native.
 * You can use this module directly, or just require InitializeCore.
 */
if (__DEV__) {
  if (!Platform.isTesting) {
    const HMRClient =
      require('basalt-core/upstream/Libraries/Utilities/HMRClient').default;

    // TODO(T214991636): Remove legacy Metro log forwarding
    if (console._isPolyfilled) {
      // We assume full control over the console and send JavaScript logs to Metro.
      (
        [
          'trace',
          'info',
          'warn',
          'error',
          'log',
          'group',
          'groupCollapsed',
          'groupEnd',
          'debug',
        ]
      ).forEach(level => {
        const originalFunction = console[level];
        console[level] = function (...args: unknown[]) {
          HMRClient.log(level, args);
          originalFunction.apply(console, args);
        };
      });
    }
  }

  require('basalt-core/upstream/Libraries/Core/setUpReactRefresh');

  // Metro's own global, whose name carries a configurable prefix, so it cannot
  // be a declared property of anything.
  (global as unknown as Record<string, unknown>)[
    `${(global as unknown as {__METRO_GLOBAL_PREFIX__?: string}).__METRO_GLOBAL_PREFIX__ ?? ''}__loadBundleAsync`
  ] =
    require('basalt-core/upstream/Libraries/Core/Devtools/loadBundleFromServer').default;

  // --- basalt-core -------------------------------------------------
  //
  // The host's half of the developer menu. `basaltDevMenu` and `reload` are
  // spelled in native/core/DevMenu.h, which is the only other place they
  // appear.
  const RCTDeviceEventEmitter =
    require('basalt-core/upstream/Libraries/EventEmitter/RCTDeviceEventEmitter').default;
  const DevSettings =
    require('basalt-core/upstream/Libraries/Utilities/DevSettings').default;

  RCTDeviceEventEmitter.addListener('basaltDevMenu', (action: string) => {
    switch (action) {
      case 'reload':
        DevSettings.reload('dev menu');
        break;
      default:
        console.warn(`basalt: unknown dev menu action '${action}'`);
    }
  });
}
