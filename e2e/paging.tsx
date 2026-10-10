/**
 * The `<ScrollView>` props that decide where a fling *ends*.
 *
 * Three lists, because three different things were broken and each is a
 * different seam:
 *
 *   paged    `pagingEnabled`, which React Native's own ScrollView computes
 *            with `Platform.select({ios, android})` and no `default`, so it
 *            reached no desktop at all. basalt-core's ScrollView override
 *            turns it into the snap interval the viewport is; see its header.
 *   fast     `decelerationRate="fast"`, one of two words resolved through the
 *            same kind of table, so both arrived as `undefined` and the host
 *            kept React Native's `'normal'` rate.
 *   offsets  `snapToOffsets`, which was never broken and is here so the
 *            scenario can tell "the props arrive" from "this one prop
 *            arrives".
 *
 * None of this is visible in a frame: snapping changes where a list settles
 * after a gesture, and a tree dump is taken when nothing is moving. So each
 * host prints what it was told, in words core/ScrollSnap.h formats for all
 * three.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, ScrollView, StyleSheet, Text, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const PAGES = ['one', 'two', 'three'];

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#ffffff', padding: 20},
  label: {fontSize: 12, color: '#555555', marginBottom: 4},
  // A fixed width, so the page the wrapper measures is a number this scenario
  // can predict from the dump.
  paged: {width: 300, height: 90, marginBottom: 16, backgroundColor: '#eef2ff'},
  fast: {width: 300, height: 90, marginBottom: 16, backgroundColor: '#f0fdf4'},
  offsets: {width: 300, height: 90, backgroundColor: '#fef2f2'},
  card: {width: 300, height: 90, justifyContent: 'center'},
  cardLabel: {fontSize: 20, textAlign: 'center', color: '#1f2937'},
});

function Pages() {
  return (
    <>
      {PAGES.map(name => (
        <View key={name} style={styles.card}>
          <Text style={styles.cardLabel}>{name}</Text>
        </View>
      ))}
    </>
  );
}

function App() {
  return (
    <View style={styles.page}>
      <Text style={styles.label}>pagingEnabled</Text>
      <ScrollView horizontal={true} pagingEnabled={true} style={styles.paged}>
        <Pages />
      </ScrollView>

      <Text style={styles.label}>decelerationRate fast, snapToInterval 150</Text>
      <ScrollView
        horizontal={true}
        decelerationRate="fast"
        snapToInterval={150}
        style={styles.fast}>
        <Pages />
      </ScrollView>

      <Text style={styles.label}>snapToOffsets</Text>
      <ScrollView
        horizontal={true}
        snapToOffsets={[0, 300, 600]}
        snapToEnd={false}
        style={styles.offsets}>
        <Pages />
      </ScrollView>
    </View>
  );
}

AppRegistry.registerComponent('BasaltPaging', () => App);
