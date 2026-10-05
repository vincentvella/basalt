'use strict';
import * as React from 'react';
import {AppRegistry, StyleSheet, View} from 'react-native';
console.log('absoluteFillObject is ' + JSON.stringify(StyleSheet.absoluteFillObject));
console.log('absoluteFill is ' + JSON.stringify(StyleSheet.absoluteFill));
const s = StyleSheet.create({
  page: {flex: 1, padding: 24, backgroundColor: '#111'},
  row: {height: 110, marginBottom: 12},
  fill: {...StyleSheet.absoluteFillObject, backgroundColor: '#4285f4'},
});
function App() {
  return (
    <View style={s.page}>
      <View style={s.row}><View style={s.fill} /></View>
    </View>
  );
}
AppRegistry.registerComponent('BasaltAbs', () => App);
