/**
 * `requestAnimationFrame`, at the display's rate rather than as fast as it can.
 *
 * React Native's own is `setTimeout(0)`, so a loop that asks for the next frame
 * from inside its callback -- Animated's JS driver, any render or playback
 * loop -- ran thousands of times a second. This asks for a second's worth of
 * frames that way and says how many it got, whether two callbacks in one frame
 * saw the same timestamp, and whether a cancelled one stayed cancelled.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';

// Globals React Native installs and its TypeScript API does not declare, and
// this project's e2e has no DOM library to borrow them from.
declare function requestAnimationFrame(callback: (now: number) => void): number;
declare function cancelAnimationFrame(id: number): void;

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#ffffff', padding: 24},
  label: {fontSize: 14, color: '#111827'},
});

function measure() {
  let frames = 0;
  const start = performance.now();
  const loop = (now: number) => {
    frames += 1;
    if (now - start < 1000) {
      requestAnimationFrame(loop);
    } else {
      console.log(`raf: ${frames} frames in ${Math.round(now - start)}ms`);
      sameFrame();
    }
  };
  requestAnimationFrame(loop);
}

function sameFrame() {
  const seen: number[] = [];
  const both = (now: number) => {
    seen.push(now);
    if (seen.length === 2) {
      console.log(`raf: one frame, one timestamp ${String(seen[0] === seen[1])}`);
      cancelled();
    }
  };
  requestAnimationFrame(both);
  requestAnimationFrame(both);
}

function cancelled() {
  let ran = false;
  const id = requestAnimationFrame(() => {
    ran = true;
  });
  cancelAnimationFrame(id);
  setTimeout(() => console.log(`raf: cancelled ran ${String(ran)}`), 300);
}

function App() {
  React.useEffect(measure, []);
  return (
    <View style={styles.page}>
      <Text style={styles.label}>requestAnimationFrame</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltRaf', () => App);
