/**
 * Reanimated and worklets, on the desktop.
 *
 * The gap this closes: Reanimated support is opt-in, compiled only when an app
 * has the library, and until this app existed no CI job had one. So nothing
 * built it. The glue drifted from Reanimated 4.5-4.6 to a 4.7 that had moved
 * three interfaces, and what found that was somebody's expo-router app rather
 * than this repository.
 *
 * What it asserts is the part a compile cannot: that a worklet actually runs on
 * the UI runtime and that what it computes reaches the view layer.
 *
 * Two independent claims, because either alone can pass while the other is
 * broken:
 *
 *   1. `runOnUI` runs a worklet over there and `runOnJS` brings an answer back.
 *      A module that installed and did nothing fails this.
 *   2. `useAnimatedStyle` drives a width to a value the dumped view tree can be
 *      read for. This is the one that caught `attachToRNRuntime` going
 *      unwired: the build was clean and the first worklet died reaching for a
 *      global nothing had installed.
 *
 * The numbers are deliberate. A width animating 100 -> 321 is asserted at 321,
 * so a run where the animation never started reports 100 and a run where
 * Reanimated committed a half-finished frame reports something in between --
 * both distinguishable from success, which `width: 321` as a plain style would
 * not be.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';
import Animated, {
  runOnJS,
  runOnUI,
  useAnimatedStyle,
  useSharedValue,
  withTiming,
} from 'react-native-reanimated';

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#16181d', padding: 24},
  line: {color: '#f7f6f3', fontSize: 16, height: 24},
  // No width: the width is the animated property, and a static one here would
  // make the assertion pass without Reanimated.
  bar: {height: 17, backgroundColor: '#ff00aa'},
});

function App() {
  const [lines, setLines] = React.useState<string[]>([]);
  const width = useSharedValue(100);

  const say = React.useCallback((line: string) => {
    setLines(previous => [...previous, line]);
    console.log(`reanimated: ${line}`);
  }, []);

  React.useEffect(() => {
    // Claim 1: a worklet runs on the UI runtime and answers.
    //
    // The arithmetic is there so the answer cannot come from anywhere but the
    // worklet body, and `_WORKLET` is Reanimated's own marker for "this is
    // executing on a worklet runtime" -- a worklet that silently ran on the
    // JavaScript thread would report false and fail.
    runOnUI((start: number) => {
      'worklet';
      const onUi = global._WORKLET === true;
      runOnJS(say)(`worklet ran: onUi=${onUi} answer=${start * 3 + 21}`);
    })(100);

    // Claim 2: an animation drives a style the view layer mounts.
    width.value = withTiming(321, {duration: 400});
    const settled = setTimeout(() => say('animation settled'), 1200);
    return () => clearTimeout(settled);
  }, [say, width]);

  const animated = useAnimatedStyle(() => ({width: width.value}));

  return (
    <View style={styles.page}>
      <Animated.View style={[styles.bar, animated]} />
      {lines.map(line => (
        <Text key={line} style={styles.line}>
          {line}
        </Text>
      ))}
    </View>
  );
}

AppRegistry.registerComponent('BasaltReanimated', () => App);
