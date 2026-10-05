/**
 * An app that would rather not be quit just yet.
 *
 * `useCloseRequest()` guards a window; this guards the application, which is
 * a different event with a different route through every desktop -- macOS
 * asks `applicationShouldTerminate:` and asks no window, and a session ending
 * on Linux or Windows does the same. An app with unsaved work that guarded
 * only its windows would lose it to Cmd-Q.
 *
 * The screen reports what happened, so a scenario can read it: how many quits
 * were refused, and whether the app has since agreed to one. Agreeing ends
 * the process, so the tree that proves it is the tree written on the way out.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';
import {useQuitRequest} from 'basalt-core';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  heading: {color: '#f7f8fa', fontSize: 20, height: 28},
  line: {color: '#9aa3b2', fontSize: 15, height: 22},
});

function App() {
  const [refused, setRefused] = React.useState(0);
  // A ref as well as state: the handler runs outside React's render, and what
  // it needs to decide is the count at that moment rather than the one from
  // the render it closed over.
  const refusedRef = React.useRef(0);

  useQuitRequest(allowQuit => {
    refusedRef.current += 1;
    setRefused(refusedRef.current);
    console.log(`quit refused ${refusedRef.current}`);

    // The second ask is agreed to. One refusal proves the interception; the
    // agreement proves an app can still get out, which is the half that
    // would strand a process if it were wrong.
    if (refusedRef.current >= 2) {
      console.log('quit allowed');
      allowQuit();
    }
  }, []);

  return (
    <View style={styles.page}>
      <Text style={styles.heading}>quit guard</Text>
      <Text style={styles.line}>refused {refused}</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltQuit', () => App);
