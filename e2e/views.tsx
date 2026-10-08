/**
 * A real React Native app made only of `<View>`.
 *
 * This exists to test the JavaScript platform layer without needing the view
 * layer to be finished. It imports `react-native` properly, goes through
 * `AppRegistry`, renders with React and lays out with Yoga -- so every one of
 * the overrides in `packages/basalt-core/metro-config.js` has to work
 * for it to run at all. What it deliberately avoids is `<Text>`, `<Image>`,
 * `<ScrollView>` and `<TextInput>`, none of which macOS can mount yet.
 *
 * The layout is nested flex on purpose. A flat row would produce a tree that
 * agrees between two platforms by accident; this one only agrees if Yoga ran
 * on identical input and the mutations arrived in the same order.
 *
 * Nothing here branches on the platform, because `scripts/compare_hosts.sh`
 * diffs the two trees and a difference must mean a bug rather than a choice.
 * `Platform.OS` is logged instead, which is how the same script checks that
 * each host got the platform it asked for.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {
    flex: 1,
    backgroundColor: '#1f2129',
    padding: 24,
  },
  row: {
    flexDirection: 'row',
    flex: 2,
  },
  left: {
    flex: 2,
    backgroundColor: '#4d8cf2',
    padding: 16,
    marginRight: 16,
    borderRadius: 8,
  },
  right: {
    flex: 1,
    backgroundColor: '#f27359',
    borderRadius: 8,
  },
  // A child that overflows its parent, which is React Native's default and one
  // of the first things a new view layer gets wrong.
  badge: {
    width: 120,
    height: 48,
    marginTop: 120,
    backgroundColor: '#e6eeff',
    opacity: 0.85,
  },
  footer: {
    flex: 1,
    marginTop: 16,
    backgroundColor: '#59cc8c',
    flexDirection: 'row',
    alignItems: 'center',
    justifyContent: 'space-between',
    paddingHorizontal: 16,
  },
  dot: {
    width: 40,
    height: 40,
    borderRadius: 20,
    backgroundColor: '#1f2129',
  },
  // borderStyle, which is the one border prop that changes how a border is
  // drawn rather than what colour it is: both hosts stroke a dashed outline
  // instead of filling four edges. Here rather than in a host-specific app
  // because the two trees are diffed, and a style that arrived on one desktop
  // and not the other is exactly what that diff is for.
  dashed: {
    width: 40,
    height: 40,
    borderWidth: 3,
    borderColor: '#1f2129',
    borderStyle: 'dashed',
  },
  dotted: {
    width: 40,
    height: 40,
    borderWidth: 3,
    borderColor: '#1f2129',
    borderStyle: 'dotted',
  },
  // cursor, the other prop that is invisible in a picture of a still window.
  // Two keywords rather than one: a plain one, and a hyphenated one that each
  // host has to spell its own way -- GDK takes it as it stands and macOS has to
  // find an NSCursor for it.
  handy: {
    width: 40,
    height: 40,
    backgroundColor: '#4d8cf2',
    cursor: 'pointer',
  },
  // backgroundImage: a CSS gradient, which is what React Native put behind that
  // prop. The angle is deliberately not a right angle and the box is not square,
  // so the gradient line is the spec's perpendicular construction rather than
  // the diagonal -- the two differ by enough to see.
  gradient: {
    width: 80,
    height: 40,
    backgroundImage: 'linear-gradient(135deg, #4d8cf2 0%, #e0484d 100%)',
  },
  // filter: a CSS filter list, which both hosts resolve through the same shared
  // arithmetic and then hand to a colour-matrix node or a Core Image filter.
  // Two functions, because a list is one matrix and the composition is the part
  // worth proving crosses the seam.
  filtered: {
    width: 40,
    height: 40,
    backgroundColor: '#4d8cf2',
    filter: 'grayscale(1) brightness(1.2)',
  },
  // boxShadow, the other prop that only a dump can confirm. Two shadows and an
  // inset one, because the list and the inset flag are each a thing a host can
  // drop while still drawing something plausible.
  shadowed: {
    width: 40,
    height: 40,
    backgroundColor: '#ffffff',
    boxShadow: '0 4px 8px rgba(0, 0, 0, 0.25), inset 0 1px 0 #ffffff',
  },
  draggable: {
    width: 40,
    height: 40,
    backgroundColor: '#4d8cf2',
    // Cast because React Native's own `CursorValue` is `'auto' | 'pointer'` and
    // nothing else, while its C++ parses all thirty-four CSS keywords and its
    // ViewProps carries them. The type is the narrow half, not the platform:
    // see backlog/upstream.md.
    cursor: 'ns-resize' as unknown as 'pointer',
  },
});

function App() {
  return (
    <View style={styles.page}>
      <View style={styles.row}>
        <View style={styles.left}>
          <View style={styles.badge} />
        </View>
        <View style={styles.right} />
      </View>
      <View style={styles.footer}>
        <View style={styles.dot} />
        <View style={styles.dashed} />
        <View style={styles.dotted} />
        <View style={styles.filtered} />
        <View style={styles.gradient} />
        <View style={styles.shadowed} />
        <View style={styles.handy} />
        <View style={styles.draggable} />
        <View style={styles.dot} />
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltViews', () => App);
