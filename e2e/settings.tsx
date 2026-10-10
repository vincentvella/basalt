/**
 * `Settings`, across two runs of the host.
 *
 * One run cannot test this. The whole claim is that a value an app writes is
 * there the next time the application starts, so the scenario runs this app
 * twice against one settings file and this file behaves differently the second
 * time: the first run finds nothing and writes, the second finds what the first
 * wrote and says so.
 *
 * It covers both halves of what was missing, and either one breaking shows up
 * here. Without the native module, `NativeSettingsManager` is a `getEnforcing`
 * lookup that throws at import and the app does not render at all. Without
 * src/overrides/Settings.js, React Native hands the app `SettingsFallback` --
 * four methods that `console.warn` and answer null -- so the second run finds
 * nothing and the log says `unset` where it should say a number.
 *
 * `deleteValues` is reached through the TurboModule proxy rather than through
 * `Settings`, because `Settings` has no delete method: it is the one method of
 * the three an app can only get to by asking for the module. The scenario reads
 * the settings file afterwards, which is where the evidence for all three
 * methods is.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Settings, StyleSheet, View} from 'react-native';
import {messageOf} from './errors';

console.log(`Platform.OS is ${Platform.OS}`);

// What the previous run left, read before anything is written. `Settings.get`
// is synchronous, which is only possible because the whole store arrived with
// `getConstants()` at import; see src/overrides/Settings.ts.
const runsBefore = Settings.get('runs');
const tabBefore = Settings.get('tab');
console.log(`settings runs before: ${runsBefore === undefined ? 'unset' : String(runsBefore)}`);
console.log(`settings tab before: ${tabBefore === undefined ? 'unset' : String(tabBefore)}`);

const runs = typeof runsBefore === 'number' ? runsBefore : 0;

if (runs === 0) {
  // The first run. Three shapes, because what the store has to survive is not
  // only strings: a number read back as a string would break the `typeof`
  // above, and an object is what an app keeps a window's last position in.
  Settings.set({runs: 1, tab: 'inbox', window: {x: 10, y: 20}});
} else {
  Settings.set({runs: runs + 1});

  // A null forgets the key rather than storing a null, which is what iOS does
  // and what an app that calls `Settings.set({tab: null})` means. The proof is
  // in the file: the copy in JavaScript would read undefined either way.
  Settings.set({tab: null});

  // The method `Settings` cannot reach. An app that wants it asks for the
  // module, which is what this does.
  const proxy = global.__turboModuleProxy;
  // The proxy answers `object | null`, because what a module has on it is not
  // something a bundle can know; the one method this calls is asserted instead.
  type SettingsManager = {deleteValues?: (keys: Array<string>) => void};
  let deleted = 'no proxy';
  try {
    const module =
      typeof proxy === 'function' ? (proxy('SettingsManager') as SettingsManager | null) : null;
    if (module == null || typeof module.deleteValues !== 'function') {
      deleted = 'no module';
    } else {
      module.deleteValues(['window']);
      deleted = 'asked';
    }
  } catch (error) {
    deleted = `threw: ${messageOf(error)}`;
  }
  console.log(`settings deleteValues: ${deleted}`);
}

console.log(`settings runs after: ${String(Settings.get('runs'))}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24, flexDirection: 'row'},
  pip: {width: 40, height: 40, borderRadius: 20, marginRight: 12, backgroundColor: '#56c98a'},
});

function App() {
  // One box per run the store remembers, so the widget tree says how many
  // without needing a text engine.
  const seen = typeof Settings.get('runs') === 'number' ? (Settings.get('runs') as number) : 0;
  return (
    <View style={styles.page}>
      {Array.from({length: seen}, (_, index) => (
        <View key={index} style={styles.pip} />
      ))}
    </View>
  );
}

AppRegistry.registerComponent('BasaltSettings', () => App);
