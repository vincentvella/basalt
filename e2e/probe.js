/**
 * What still throws?
 *
 * Every API below is touched inside its own try/catch, with `require` at call
 * time rather than a top-level import -- because a top-level import that throws
 * takes the whole app down, which is exactly the failure being looked for.
 *
 * This is a diagnostic rather than a test: it reports rather than asserts, and
 * what it is for is turning "some things still crash" into a list.
 *
 * `InteractionManager` is deliberately absent from it. Touching that one throws
 * "InteractionManager has been removed from react-native core", on every
 * platform -- it was in an earlier version of this file and the failure it
 * reported was upstream's intent rather than a gap here.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const PROBES = [
  // --- web APIs the runtime is supposed to install -------------------------
  ['performance.now', () => typeof performance.now() === 'number'],
  ['performance.mark', () => {
    performance.mark('probe');
    return true;
  }],
  ['queueMicrotask', () => {
    queueMicrotask(() => {});
    return true;
  }],
  ['requestIdleCallback', () => {
    if (typeof requestIdleCallback !== 'function') {
      return 'absent';
    }
    requestIdleCallback(() => {});
    return true;
  }],
  ['requestAnimationFrame', () => {
    requestAnimationFrame(() => {});
    return true;
  }],
  ['setTimeout', () => {
    setTimeout(() => {}, 0);
    return true;
  }],
  ['fetch exists', () => typeof fetch === 'function'],
  ['WebSocket exists', () => typeof WebSocket === 'function'],
  ['URL', () => new URL('https://example.com/a?b=c').host === 'example.com'],
  ['TextEncoder', () =>
    typeof TextEncoder === 'function' ? new TextEncoder().encode('x').length === 1 : 'absent'],

  // --- React Native modules -------------------------------------------------
  ['AppState', () => require('react-native').AppState.currentState !== undefined],
  ['UIManager', () => typeof require('react-native').UIManager === 'object'],
  ['Settings', () => {
    const {Settings} = require('react-native');
    Settings.get('nothing');
    return true;
  }],
  ['Share', () => typeof require('react-native').Share.share === 'function'],
  ['BackHandler', () => {
    const {BackHandler} = require('react-native');
    const sub = BackHandler.addEventListener('hardwareBackPress', () => false);
    sub?.remove?.();
    return true;
  }],
  ['ToastAndroid', () => {
    const {ToastAndroid} = require('react-native');
    ToastAndroid.show('probe', ToastAndroid.SHORT);
    return true;
  }],
  ['PixelRatio', () => typeof require('react-native').PixelRatio.get() === 'number'],
  ['Dimensions', () => require('react-native').Dimensions.get('window').width > 0],
  ['DevSettings', () => {
    // Reachable from ordinary code, and absent from a release bundle until
    // phase 32 -- upstream provides it only when a dev server exists.
    const {DevSettings} = require('react-native');
    return typeof DevSettings.reload === 'function';
  }],
  ['LayoutAnimation', () => {
    const {LayoutAnimation} = require('react-native');
    LayoutAnimation.configureNext(LayoutAnimation.Presets.easeInEaseOut);
    return true;
  }],
  ['PanResponder', () => typeof require('react-native').PanResponder.create === 'function'],
  ['Animated.timing runs', () => {
    const {Animated} = require('react-native');
    const value = new Animated.Value(0);
    Animated.timing(value, {toValue: 1, duration: 1, useNativeDriver: true}).start();
    return true;
  }],
  ['ActivityIndicator renders', () => {
    const {ActivityIndicator} = require('react-native');
    return typeof ActivityIndicator === 'object' || typeof ActivityIndicator === 'function';
  }],
  ['Switch renders', () => {
    const {Switch} = require('react-native');
    return typeof Switch === 'object' || typeof Switch === 'function';
  }],
  ['Modal renders', () => {
    const {Modal} = require('react-native');
    return typeof Modal === 'object' || typeof Modal === 'function';
  }],
  ['RefreshControl', () => {
    const {RefreshControl} = require('react-native');
    return typeof RefreshControl === 'object' || typeof RefreshControl === 'function';
  }],
  ['FlatList', () => {
    const {FlatList} = require('react-native');
    return typeof FlatList === 'object' || typeof FlatList === 'function';
  }],
];

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#1f2129', padding: 24, flexDirection: 'row', flexWrap: 'wrap'},
  pip: {width: 18, height: 18, borderRadius: 9, margin: 4, backgroundColor: '#59cc8c'},
  bad: {backgroundColor: '#f27359'},
});

function App() {
  const [results, setResults] = React.useState([]);

  React.useEffect(() => {
    const out = [];
    for (const [name, probe] of PROBES) {
      try {
        const value = probe();
        if (value === 'absent') {
          console.log(`ABSENT: ${name}`);
          out.push(false);
        } else if (value) {
          console.log(`ok:     ${name}`);
          out.push(true);
        } else {
          console.log(`FALSE:  ${name}`);
          out.push(false);
        }
      } catch (error) {
        console.log(`THREW:  ${name} -- ${error.message}`);
        out.push(false);
      }
    }
    console.log(`probes: ${out.filter(Boolean).length}/${out.length}`);
    setResults(out);
  }, []);

  return (
    <View style={styles.page}>
      {results.map((ok, index) => (
        <View key={index} style={[styles.pip, !ok && styles.bad]} />
      ))}
    </View>
  );
}

AppRegistry.registerComponent('BasaltProbe', () => App);
