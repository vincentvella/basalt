/**
 * An Expo app that calls `fetch`.
 *
 * Which is every Expo app, and which used to be the point where one died here.
 * Expo replaces `globalThis.fetch` with its own WinterCG implementation, built
 * on a native module called `ExpoFetchModule` that this platform has no port
 * of. The replacement is installed as a lazy getter, so nothing fails at import
 * and everything fails at the first call -- inside the global the app was
 * calling, where the app cannot catch it. kino died exactly there, with
 * `Cannot find native module 'ExpoFetchModule'` and then a `TypeError` from the
 * component whose data never arrived.
 *
 * What this asserts is that the platform's default *arrives*: that
 * `EXPO_PUBLIC_USE_RN_FETCH` is set before the bundle runs and survives both
 * Metro's prelude and React Native's setUpGlobals, and that `fetch` is then
 * React Native's own and works. See native/core/ExpoRuntime.cpp's
 * `useReactNativeFetch`.
 *
 * **What it does not assert** is expo honouring it, because this app does not
 * import `expo` -- and adding the import does not help: this repository's own
 * bundler does not pull expo's winter runtime into a e2e/ app at all, so the
 * replacement never runs here whatever the variable says. That half is expo's
 * code, and it was checked by running kino: with the default the app boots
 * clean, and without it the first `fetch` dies naming the module.
 *
 * Every step is logged, which is what the end-to-end suite reads.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  pip: {width: 28, height: 28, borderRadius: 14, backgroundColor: '#56c98a'},
});

function App() {
  const [done, setDone] = React.useState(false);

  React.useEffect(() => {
    (async () => {
      // The variable expo's own runtime reads to decide whether to replace
      // fetch. Logged rather than assumed: it is set from native before the
      // bundle runs, and a bundler that clobbered `process.env` would show up
      // here rather than as a crash three lines later.
      console.log(`fetch: useRnFetch ${process.env.EXPO_PUBLIC_USE_RN_FETCH}`);

      // Reading the global is the operation that used to throw. Doing it in its
      // own statement so that a failure here is distinguishable from a failure
      // in the call below.
      let implementation;
      try {
        implementation = globalThis.fetch;
        console.log(`fetch: global ${typeof implementation}`);
      } catch (error) {
        console.log(`fetch: global threw ${error.message}`);
        setDone(true);
        return;
      }

      // And whether it is expo's. Expo brands what it installs with a
      // well-known symbol, so this asks rather than infers.
      const branded = Boolean(
        implementation && implementation[Symbol.for('expo.builtin')],
      );
      console.log(`fetch: expo ${branded}`);

      // And then actually call it, because a `fetch` that is present and does
      // nothing would pass everything above.
      //
      // Port 1 rather than a server of our own: nothing listens there, on any
      // machine, so the outcome is the same everywhere and there is no port to
      // collide over. What is being asserted is that the call reached a real
      // implementation and came back as a *rejected promise* -- which is what a
      // connection refused looks like -- rather than throwing where it was
      // called, which is what the broken global did.
      try {
        const response = await fetch('http://127.0.0.1:1/');
        console.log(`fetch: status ${response.status}`);
      } catch (error) {
        console.log(`fetch: rejected ${error.message}`);
      }
      setDone(true);
    })();
  }, []);

  return (
    <View style={styles.page}>{done ? <View style={styles.pip} /> : null}</View>
  );
}

AppRegistry.registerComponent('BasaltExpoFetch', () => App);
