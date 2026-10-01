/**
 * An app about the screens it is on.
 *
 * React Native has no API for this: on a phone there is one display, it is
 * the screen, and `Dimensions` is the answer. On a desktop it is a question
 * with a list for an answer, and the list changes while the app is running.
 *
 * Every number on screen is asserted by the scenario, so the layout is fixed
 * heights: what matters is that a display exists, that its scale factor is
 * sane, and that the work area is inside the bounds rather than outside them.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';
import {useDisplays} from 'basalt-core';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  heading: {color: '#f7f8fa', fontSize: 20, height: 28},
  line: {color: '#9aa3b2', fontSize: 15, height: 22},
});

function App() {
  const screens = useDisplays();

  React.useEffect(() => {
    // Logged as well as rendered: the scenario reads the tree, and a person
    // running this by hand wants the numbers without squinting.
    for (const screen of screens) {
      console.log(
        `display ${screen.width}x${screen.height} at ${screen.x},${screen.y} ` +
          `scale ${screen.scaleFactor} primary ${screen.primary}`,
      );
    }
  }, [screens]);

  return (
    <View style={styles.page}>
      <Text style={styles.heading}>displays {screens.length}</Text>
      {screens.map((screen, index) => (
        <Text key={index} style={styles.line}>
          {`${screen.width}x${screen.height} scale ${screen.scaleFactor}` +
            `${screen.primary ? ' primary' : ''}`}
        </Text>
      ))}
    </View>
  );
}

AppRegistry.registerComponent('BasaltDisplays', () => App);
