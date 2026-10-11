/**
 * `AppState`, which told every desktop app nothing at all.
 *
 * ReactCxxPlatform provides the module and leaves every method of it empty:
 * `getConstants` answers `initialAppState: ""`, `getCurrentAppState` calls
 * neither of its callbacks, and nothing emits a change -- so
 * `AppState.currentState` was the empty string, which is not one of React
 * Native's own states, and `addEventListener('change')` never fired.
 *
 * The app minimises itself through this platform's own window API, because
 * that is a state change a scenario can cause without a window manager, a
 * second application or a person. What is asserted is what React Native's
 * `AppState` says about it.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, AppState, Platform, Pressable, StyleSheet, Text, View} from 'react-native';
import {useWindow} from 'basalt-core';

console.log(`Platform.OS is ${Platform.OS}`);
console.log(`appstate at import: ${String(AppState.currentState)}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#ffffff', padding: 24},
  label: {fontSize: 14, color: '#111827'},
  button: {
    marginTop: 16,
    height: 44,
    width: 260,
    borderRadius: 8,
    backgroundColor: '#0f766e',
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: {color: '#ffffff', fontSize: 15},
});

function App() {
  const [state, setState] = React.useState(String(AppState.currentState));
  const window = useWindow();

  React.useEffect(() => {
    console.log(`appstate on mount: ${String(AppState.currentState)}`);
    const subscription = AppState.addEventListener('change', next => {
      console.log(`appstate -> ${next}`);
      setState(String(next));
    });
    return () => subscription.remove();
  }, []);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>{`appState ${state}`}</Text>
      <Pressable
        style={styles.button}
        onPress={() => {
          // The one state change a scenario can cause by itself: it cannot
          // click on another application, and nothing else takes an app off
          // the screen. The host quits on its own timer afterwards, so
          // nothing has to put the window back.
          window.minimize();
        }}>
        <Text style={styles.buttonText}>minimise</Text>
      </Pressable>
    </View>
  );
}

AppRegistry.registerComponent('BasaltAppState', () => App);
