/**
 * An app that declares its shortcuts the way react-native-macos does.
 *
 * `keyDownEvents`, `onKeyDown` and `focusable` spread onto a plain `<View>`,
 * with no component from this package anywhere -- which is the point. An app
 * written for react-native-macos should not have to be rewritten to run here,
 * and kino's `hotkeyViewProps()` is exactly this shape.
 *
 * The other half, `<KeyHandler>`, is js/keys.js. Both are one implementation;
 * see src/useHandledKeys.ts.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  line: {color: '#ececf1', fontSize: 16},
});

// The same shape kino's hotkey registry produces.
const HOTKEYS = [
  {key: 'j', altKey: false, metaKey: false, shiftKey: false},
  {key: ' ', altKey: false, metaKey: false, shiftKey: false},
  {key: 'z', altKey: false, metaKey: true, shiftKey: false},
];

function App() {
  const [pressed, setPressed] = React.useState([]);

  const onKeyDown = React.useCallback(event => {
    // `event.nativeEvent`, which is react-native-macos's shape: an app written
    // for it reads the key from there and would see undefined otherwise.
    const key = event.nativeEvent.key;
    console.log(`macoskeys: down ${JSON.stringify(key)} meta=${!!event.nativeEvent.metaKey}`);
    setPressed(before => [...before, key]);
  }, []);

  return (
    <View
      focusable={true}
      keyDownEvents={HOTKEYS}
      onKeyDown={onKeyDown}
      style={styles.page}>
      <Text style={styles.line}>macos keys</Text>
      <Text style={styles.line}>{`pressed: ${pressed.join(',')}`}</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltMacosKeys', () => App);
