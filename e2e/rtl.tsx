/**
 * A layout that has to mirror when the desktop reads right to left.
 *
 * `I18nManager.isRTL` was false on every desktop unless an app called
 * `forceRTL`, and the hosts passed `LayoutDirection::LeftToRight` to Yoga
 * whatever it said -- so an app in Arabic laid out left to right, and an app
 * that forced the flag and reloaded, which is what React Native's own
 * documentation says to do, got a flag that disagreed with its own layout.
 *
 * Three things are on the screen, and each mirrors differently:
 *
 *   row        three boxes in a `flexDirection: 'row'`, which Yoga reverses
 *   padded     `paddingStart`, which becomes padding on the right
 *   text       `textAlign: 'start'`, which is the right edge
 *
 * The locale comes from `BASALT_TEST_LOCALE`, since a scenario cannot change
 * the machine's language; see core/SystemLocale.h.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, I18nManager, Platform, StyleSheet, Text, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);
const constants = I18nManager.getConstants();
console.log(
  `i18n isRTL ${String(constants.isRTL)} locale ${String(constants.localeIdentifier)}`,
);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#ffffff', padding: 24},
  row: {flexDirection: 'row', marginBottom: 16},
  first: {width: 80, height: 40, backgroundColor: '#2563eb'},
  second: {width: 80, height: 40, backgroundColor: '#16a34a'},
  third: {width: 80, height: 40, backgroundColor: '#dc2626'},
  padded: {paddingStart: 120, marginBottom: 16},
  inside: {width: 60, height: 30, backgroundColor: '#a855f7'},
  text: {fontSize: 16, color: '#111827', textAlign: 'start'},
});

function App() {
  return (
    <View style={styles.page}>
      <View style={styles.row}>
        <View style={styles.first} />
        <View style={styles.second} />
        <View style={styles.third} />
      </View>
      <View style={styles.padded}>
        <View style={styles.inside} />
      </View>
      <Text style={styles.text}>which edge is the start</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltRtl', () => App);
