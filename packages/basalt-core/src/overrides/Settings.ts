/**
 * `Settings`, for a platform React Native's own copy has no branch for.
 *
 * The third of these, and found by the native-module audit rather than by an
 * app: `docs/platform-modules.json` recorded `SettingsManager` as a module that
 * *throws* here, which was wrong in the app's favour and wrong in a way that
 * mattered. `Settings.js` reads
 *
 *     if (Platform.OS === 'ios') {
 *       Settings = require('./Settings').default;
 *     } else {
 *       Settings = require('./SettingsFallback').default;
 *     }
 *
 * and `SettingsFallback` is four methods that `console.warn` and answer null.
 * So nothing threw, nothing was stored, and nothing said why beyond a warning
 * that reads like a platform limitation rather than a missing module. The
 * native module was never reached, exactly as with `Share` and `Alert`.
 *
 * ## What this file is
 *
 * React Native's own `Settings.ios.js`, kept as close to the original as the
 * conversion to TypeScript allows, because its behaviour is the contract: an
 * app written against iOS reads `Settings.get` synchronously, and that only
 * works because the whole store arrives once in `getConstants()` and is cached
 * in `_settings` here. A rewrite that asked the native side per key would be a
 * different API with the same name.
 *
 * Importing the iOS file instead was the other option and is worse: the file
 * would still resolve through `./NativeSettingsManager`, and a platform would
 * depend on a `.ios` suffix meaning "iOS or anything like it", which is the
 * assumption this whole override table exists to undo.
 *
 * ## watchKeys
 *
 * Registered and never fired, and that is iOS's shape rather than a stub.
 * `Settings.set` merges into `_settings` before calling the module, so
 * `_sendObservations` sees no change for the app's own write -- which is why
 * `RCTSettingsManager` sets `_ignoringUpdates` around `setValues` instead of
 * emitting. What fires a watcher on iOS is a change from *outside* the process,
 * which on a desktop means watching the settings file; see
 * docs/backlog/modules.md for the three calls that would do it. The listener is
 * here so that the day a host emits `settingsUpdated`, this half already works.
 *
 * @format
 */

'use strict';

// `NativeSettingsManager`, which is a `getEnforcing` lookup: if a host has not
// registered `SettingsManager`, importing this throws and names the module,
// which is a better failure than the silent null the fallback gave.
import NativeSettingsManager from 'basalt-core/upstream/Libraries/Settings/NativeSettingsManager';
// The device event emitter, which is where a `settingsUpdated` event would
// arrive. React Native's own file subscribes at import and so does this one.
import RCTDeviceEventEmitter from 'basalt-core/upstream/Libraries/EventEmitter/RCTDeviceEventEmitter';

const invariant = require('invariant');

type Subscription = {
  keys: Array<string>;
  callback: (() => void) | null;
};

const subscriptions: Array<Subscription> = [];

/**
 * A persistent key-value store. On iOS this wraps `NSUserDefaults`; here it is
 * a JSON file under the per-user configuration directory, which is what
 * core/SettingsStore.h argues for.
 *
 * @see https://reactnative.dev/docs/settings
 */
const Settings = {
  _settings: NativeSettingsManager.getConstants().settings as {[key: string]: unknown},

  /**
   * The current value for `key`, or undefined when it has never been set.
   */
  get(key: string): unknown {
    return this._settings[key];
  },

  /**
   * Merges `settings` into the store, one key deep. A key set to null is
   * forgotten rather than stored as null, which is what iOS does.
   */
  set(settings: {[key: string]: unknown}): void {
    this._settings = Object.assign(this._settings, settings);
    NativeSettingsManager.setValues(settings);
  },

  /**
   * Calls `callback` whenever one of `keys` changes, and answers a watch id for
   * `clearWatch`. Nothing emits the change event on this platform yet; see the
   * header.
   */
  watchKeys(keys: string | Array<string>, callback: () => void): number {
    if (typeof keys === 'string') {
      keys = [keys];
    }

    invariant(Array.isArray(keys), 'keys should be a string or array of strings');

    const sid = subscriptions.length;
    subscriptions.push({keys, callback});
    return sid;
  },

  clearWatch(watchId: number): void {
    if (watchId < subscriptions.length) {
      subscriptions[watchId] = {keys: [], callback: null};
    }
  },

  _sendObservations(body: {[key: string]: unknown}): void {
    Object.keys(body).forEach(key => {
      const newValue = body[key];
      const didChange = this._settings[key] !== newValue;
      this._settings[key] = newValue;

      if (didChange) {
        subscriptions.forEach(sub => {
          if (sub.keys.indexOf(key) !== -1 && sub.callback) {
            sub.callback();
          }
        });
      }
    });
  },
};

RCTDeviceEventEmitter.addListener('settingsUpdated', Settings._sendObservations.bind(Settings));

export default Settings;
