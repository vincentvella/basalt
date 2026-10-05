/**
 * Deliberately tiny.
 *
 * `e2e/index.js` is the demo that exercises the platform, and the end-to-end
 * suite asserts on every part of it. This app exercises the *command*: whether
 * `react-native run-linux` can find a host, start a packager, and put an app on
 * screen from a directory that looks like somebody else's project. Keeping it
 * to one screen means a failure here is the CLI's fault and nothing else's.
 */

'use strict';

import React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';

function App() {
  return (
    <View style={styles.root}>
      <Text style={styles.heading}>run-linux</Text>
      <Text style={styles.body}>
        Started by the CLI, on {Platform.OS}.
      </Text>
    </View>
  );
}

const styles = StyleSheet.create({
  root: {flex: 1, alignItems: 'center', justifyContent: 'center', backgroundColor: '#11131a'},
  heading: {fontSize: 40, fontWeight: '700', color: '#f7f8fa'},
  body: {fontSize: 20, color: '#56c98a', marginTop: 12},
});

AppRegistry.registerComponent('BasaltExample', () => App);
