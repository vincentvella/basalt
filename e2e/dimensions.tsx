/**
 * `Dimensions` and `PixelRatio`, which every React Native app reads and which
 * this platform answered with a fiction until 2026-10-10.
 *
 * ReactCxxPlatform's `DeviceInfoModule` returns a hardcoded 1280x720 with a
 * `// TODO: Wire this to come from the actual app size` beside it, a `scale` of
 * zero and a `fontScale` the generated struct never initialises -- so
 * `PixelRatio.get()` was 0 and `PixelRatio.roundToNearestPixel(x)` was NaN, in
 * every app, on all three desktops.
 *
 * Logged rather than rendered: a tree dump cannot see a number an app computed
 * from, and what matters is that the numbers are the window's own.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  AppRegistry,
  Dimensions,
  PixelRatio,
  Platform,
  Pressable,
  StyleSheet,
  Text,
  View,
} from 'react-native';
import {useWindow} from 'basalt-core';
import type {ScaledSize} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

function report(label: string) {
  const window = Dimensions.get('window');
  const screen = Dimensions.get('screen');
  console.log(
    `${label} window ${window.width}x${window.height} scale ${window.scale} ` +
      `fontScale ${window.fontScale}`,
  );
  console.log(`${label} screen ${screen.width}x${screen.height}`);
  console.log(
    `${label} pixelRatio ${PixelRatio.get()} round ${PixelRatio.roundToNearestPixel(10.3)}`,
  );
}

report('at import');

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#ffffff', padding: 24},
  label: {fontSize: 14, color: '#111827'},
  button: {
    marginTop: 16,
    height: 44,
    width: 260,
    borderRadius: 8,
    backgroundColor: '#2563eb',
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: {color: '#ffffff', fontSize: 15},
});

function App() {
  const [size, setSize] = React.useState(Dimensions.get('window'));
  // This platform's own window API, which is how the app resizes itself: the
  // point is what React Native's `Dimensions` says afterwards.
  const window = useWindow();

  React.useEffect(() => {
    report('on mount');
    // The event React Native sends when a window is resized. Nothing emitted it
    // here before 2026-10-10, so a layout computed from `Dimensions` was stale
    // the moment somebody dragged a corner.
    const subscription = Dimensions.addEventListener('change', ({window}: {window: ScaledSize}) => {
      console.log(`changed window ${window.width}x${window.height}`);
      setSize(window);
    });
    return () => subscription.remove();
  }, []);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>{`window ${size.width}x${size.height}`}</Text>
      <Pressable style={styles.button} onPress={() => window.setSize(700, 500)}>
        <Text style={styles.buttonText}>resize to 700 x 500</Text>
      </Pressable>
    </View>
  );
}

AppRegistry.registerComponent('BasaltDimensions', () => App);
