'use strict';
import * as React from 'react';
import {AppRegistry, StyleSheet, View} from 'react-native';

// `absoluteFillObject` is on StyleSheet at runtime and absent from React
// Native's generated types, and checking that both spellings are there is what
// this app is for, so it cannot be written without naming it. Widened here
// rather than cast at each use, and deliberately optional: if the types ever
// catch up, this still compiles and the runtime check still answers.
const Styles = StyleSheet as typeof StyleSheet & {
  absoluteFillObject?: Record<string, unknown>;
};
console.log('absoluteFillObject is ' + JSON.stringify(Styles.absoluteFillObject));
console.log('absoluteFill is ' + JSON.stringify(StyleSheet.absoluteFill));
const s = StyleSheet.create({
  page: {flex: 1, padding: 24, backgroundColor: '#111'},
  row: {height: 110, marginBottom: 12},
  fill: {...Styles.absoluteFillObject, backgroundColor: '#4285f4'},
});
function App() {
  return (
    <View style={s.page}>
      <View style={s.row}><View style={s.fill} /></View>
    </View>
  );
}
AppRegistry.registerComponent('BasaltAbs', () => App);
