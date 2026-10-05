/**
 * A React Native app that imports and uses the core modules this platform was
 * missing.
 *
 * The imports alone are half the test. `Clipboard` and `Vibration` are looked
 * up with `getEnforcing`, so before phase 32 the two lines at the top of this
 * file took the whole app down before anything rendered -- no error boundary,
 * no first paint, just a dead window. The rest were looked up with `get`, which
 * returns null, so they were worse to debug: `Alert.alert()` was a dialog that
 * never appeared and `Linking.openURL()` a link that never opened, both
 * silently.
 *
 * Results are logged and rendered as a row of boxes, one per check that passed,
 * so the tree says how many without needing a text engine.
 *
 * Nothing here shows a dialog: `Alert.alert` is modal, and a test that stopped
 * for a human would never finish. That one is exercised by hand.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  AccessibilityInfo,
  AppRegistry,
  Clipboard,
  I18nManager,
  Linking,
  Platform,
  StyleSheet,
  Vibration,
  View,
} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#1f2129', padding: 24, flexDirection: 'row'},
  pip: {width: 36, height: 36, borderRadius: 18, marginRight: 10, backgroundColor: '#59cc8c'},
});

const CHECKS = [
  [
    'importing Clipboard and Vibration does not throw',
    async () => typeof Clipboard === 'object' && typeof Vibration === 'object',
  ],

  [
    'clipboard round-trips a string',
    async () => {
      const written = `basalt ${Date.now()}`;
      Clipboard.setString(written);
      return (await Clipboard.getString()) === written;
    },
  ],

  [
    'clipboard takes unicode',
    async () => {
      const written = 'héllo · 世界 · 🜂';
      Clipboard.setString(written);
      return (await Clipboard.getString()) === written;
    },
  ],

  [
    'Vibration answers rather than throwing',
    async () => {
      // Meaningless on a desktop, which is why the right implementation is one
      // that does nothing. What matters is that it returns.
      Vibration.vibrate();
      Vibration.vibrate([0, 100, 200]);
      Vibration.cancel();
      return true;
    },
  ],

  [
    'canOpenURL says yes to https and no to nonsense',
    async () => {
      const web = await Linking.canOpenURL('https://example.com');
      const nonsense = await Linking.canOpenURL('zzznotascheme:whatever');
      return web === true && nonsense === false;
    },
  ],

  [
    'getInitialURL answers with the URL the app was launched with',
    async () => {
      // Null when there was none, which is what React Native's JavaScript
      // checks for, and the URL itself when a desktop passed one -- a .desktop
      // entry's %u, a registered scheme, a shell association. Logged either
      // way, because the value is the only thing that says which happened and
      // the end-to-end suite launches this app both ways.
      const url = await Linking.getInitialURL();
      console.log(`initialURL: ${url === null ? 'null' : url}`);
      return url === null || (typeof url === 'string' && url.includes('://'));
    },
  ],

  [
    'I18nManager reports a direction',
    async () =>
      typeof I18nManager.isRTL === 'boolean' &&
      typeof I18nManager.doLeftAndRightSwapInRTL === 'boolean',
  ],

  [
    'AccessibilityInfo answers its questions',
    async () => {
      const reduceMotion = await AccessibilityInfo.isReduceMotionEnabled();
      const screenReader = await AccessibilityInfo.isScreenReaderEnabled();
      return typeof reduceMotion === 'boolean' && typeof screenReader === 'boolean';
    },
  ],

  [
    'getRecommendedTimeoutMillis hands back what it was given',
    async () => (await AccessibilityInfo.getRecommendedTimeoutMillis(3000)) === 3000,
  ],

  [
    'announceForAccessibility does not throw',
    async () => {
      AccessibilityInfo.announceForAccessibility('checking');
      return true;
    },
  ],
];

function App() {
  const [passed, setPassed] = React.useState(0);

  React.useEffect(() => {
    (async () => {
      let count = 0;
      for (const [name, check] of CHECKS) {
        try {
          const ok = await check();
          console.log(`${ok ? 'pass' : 'FAIL'}: ${name}`);
          if (ok) {
            count++;
          }
        } catch (error) {
          console.log(`FAIL: ${name} threw ${error.message}`);
        }
      }
      console.log(`module checks: ${count}/${CHECKS.length}`);
      setPassed(count);
    })();
  }, []);

  return (
    <View style={styles.page}>
      {Array.from({length: passed}, (_, index) => (
        <View key={index} style={styles.pip} />
      ))}
    </View>
  );
}

AppRegistry.registerComponent('BasaltModules', () => App);
