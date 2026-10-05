// That `globalThis.__turboModuleProxy` answers, and answers for React Native's
// own modules rather than only for this platform's.
//
// The second half is the point. A proxy that served only the modules this
// platform registers would answer null for PlatformConstants -- and on the
// versions this exists for, a null from the proxy does not fall through to
// NativeModules. So "it exists" is not the claim worth testing; "it answers for
// something React Native provides" is.
import React from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';
import {messageOf} from './errors';

const proxy = global.__turboModuleProxy;
console.log(`turbo proxy: ${typeof proxy}`);
if (typeof proxy === 'function') {
  // This platform's own, from core/.
  const ours = proxy('BasaltWindows');
  console.log(`turbo proxy BasaltWindows: ${ours == null ? 'null' : 'found'}`);
  // React Native's own, which a proxy of our own making would not have.
  const theirs = proxy('PlatformConstants');
  console.log(`turbo proxy PlatformConstants: ${theirs == null ? 'null' : 'found'}`);
  // And a name nobody provides, which must come back empty rather than throw --
  // TurboModuleRegistry.get is allowed to return null and callers rely on it.
  let unknown = 'threw';
  try {
    unknown = proxy('NoSuchModuleAtAll') == null ? 'null' : 'found';
  } catch (error) {
    unknown = `threw: ${messageOf(error)}`;
  }
  console.log(`turbo proxy unknown name: ${unknown}`);
}

function App() {
  return (
    <View style={styles.page}>
      <Text style={styles.label}>turbomodule proxy</Text>
    </View>
  );
}

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#14151a', padding: 24},
  label: {color: '#e6e6e6', fontSize: 16},
});

AppRegistry.registerComponent('BasaltTurboModules', () => App);
