/**
 * A React Native app about the window it is in.
 *
 * React Native has no API for this: on a phone there is one window, it is the
 * screen, and nothing an app says would change it. So, like js/dialogs.js, this
 * is a screen about something React Native does not do at all.
 *
 * Its own bounds are on screen and logged, which is the half worth asserting:
 * `bounds` is live, so a window resized by anything -- the person, the window
 * manager, or the buttons here -- re-renders the app with the new numbers. An
 * app that cached `getBounds()` would be right until somebody dragged a corner.
 *
 * A second app below is about how big the window may be, which is a different
 * question and not the same on every desktop -- so it starts by logging what
 * this one says it can do.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Pressable, StyleSheet, Text, View} from 'react-native';
import {useWindow} from 'basalt-core';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  // Fixed heights, so the buttons sit at the same y on all three hosts: text
  // measurement is the one thing the three text engines will never agree on.
  label: {color: '#aab', fontSize: 13, height: 22},
  row: {flexDirection: 'row', height: 48, marginBottom: 12},
  button: {
    width: 150,
    height: 48,
    marginRight: 12,
    borderRadius: 8,
    backgroundColor: '#4285f4',
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: {color: '#fff', fontSize: 15},
});

function App() {
  const window = useWindow();
  const {width, height, x, y, fullScreen, maximized} = window.bounds;

  // Logged on every change, which is what says the event arrived rather than
  // that the first read did.
  React.useEffect(() => {
    console.log(
      `window bounds: ${width}x${height} at ${x},${y} ` +
        `fullScreen=${fullScreen} maximized=${maximized}`,
    );
  }, [width, height, x, y, fullScreen, maximized]);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>Window</Text>
      <View style={styles.row}>
        <Pressable style={styles.button} onPress={() => window.setSize(700, 500)}>
          <Text style={styles.buttonText}>700 x 500</Text>
        </Pressable>
        <Pressable style={styles.button} onPress={() => window.setSize(1000, 800)}>
          <Text style={styles.buttonText}>1000 x 800</Text>
        </Pressable>
      </View>
      <View style={styles.row}>
        <Pressable style={styles.button} onPress={() => window.center()}>
          <Text style={styles.buttonText}>Centre</Text>
        </Pressable>
        <Pressable
          style={styles.button}
          onPress={() => window.setFullScreen(!fullScreen)}>
          <Text style={styles.buttonText}>
            {fullScreen ? 'Leave full screen' : 'Full screen'}
          </Text>
        </Pressable>
      </View>
      <Text style={styles.label}>
        {`${width} x ${height} at ${x}, ${y}`}
      </Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltWindow', () => App);

/**
 * How big the window may be.
 *
 * A minimum and a maximum, then two buttons that ask for sizes outside both.
 * What is worth watching is that the window does not go there: a size is a
 * request, and a limit is what the window manager answers it with.
 *
 * Its own screen rather than more buttons on the one above, because the app
 * above asks for sizes and this one constrains them, and a screen that did both
 * would have two answers to "how big is it".
 *
 * `capabilities` is logged first because two of these do nothing on Linux and
 * that is settled rather than pending -- GTK4 removed both calls, because
 * Wayland has no protocol for either. An app reads this instead of reading a
 * platform check.
 */
function Limits() {
  const window = useWindow();
  const {width, height} = window.bounds;

  React.useEffect(() => {
    const able = window.capabilities;
    console.log(
      `window capabilities: position=${able.position} minimumSize=${able.minimumSize} ` +
        `maximumSize=${able.maximumSize} resizable=${able.resizable} ` +
        `alwaysOnTop=${able.alwaysOnTop}`,
    );
    window.setMinimumSize(500, 400);
    window.setMaximumSize(800, 600);
    console.log('window limits: 500x400 to 800x600');
    // Exercised rather than asserted, and worth saying why. Neither of these is
    // observable from inside the app -- a window that cannot be resized is
    // still whatever size it is, and one that floats is still where it was -- so
    // what the run can say is that the native path executed without taking the
    // host with it, on all three. Anything more needs a person.
    window.setResizable(true);
    window.setAlwaysOnTop(false);
    console.log('window flags: resizable and always-on-top reached the host');
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  React.useEffect(() => {
    console.log(`window limited bounds: ${width}x${height}`);
  }, [width, height]);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>Window limits</Text>
      <View style={styles.row}>
        <Pressable
          style={styles.button}
          onPress={() => {
            console.log('window asked for 300x200');
            window.setSize(300, 200);
          }}>
          <Text style={styles.buttonText}>Ask for 300 x 200</Text>
        </Pressable>
        <Pressable
          style={styles.button}
          onPress={() => {
            console.log('window asked for 1400x1100');
            window.setSize(1400, 1100);
          }}>
          <Text style={styles.buttonText}>Ask for 1400 x 1100</Text>
        </Pressable>
      </View>
      <Text style={styles.label}>{`${width} x ${height}`}</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltWindowLimits', () => Limits);
