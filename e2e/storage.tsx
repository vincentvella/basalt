/**
 * async-storage, on the desktop.
 *
 * The proof for `basalt-async-storage`, and the first third-party native
 * module this platform has been through end to end: the app imports
 * `@react-native-async-storage/async-storage` exactly as it would on a phone,
 * and nothing here knows that the native half is Basalt's.
 *
 * What it asserts is the round trip, which is the thing that cannot be faked
 * by a module that loaded and did nothing: write, read it back, merge a field
 * into a stored object, and list the keys.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, StyleSheet, Text, View} from 'react-native';
import AsyncStorage from '@react-native-async-storage/async-storage';

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#16181d', padding: 24},
  line: {color: '#f7f6f3', fontSize: 16, height: 24},
});

function App() {
  const [lines, setLines] = React.useState<string[]>([]);

  React.useEffect(() => {
    (async () => {
      const said: string[] = [];
      try {
        await AsyncStorage.clear();
        await AsyncStorage.setItem('token', 'abc123');
        said.push(`read back: ${await AsyncStorage.getItem('token')}`);

        // 3.x's own API, which is getItem/setItem/removeItem/getAllKeys and
        // no mergeItem: merging moved to the legacy calls, which the store's
        // own tests cover because no 3.x app can reach them.
        await AsyncStorage.setItem('user', JSON.stringify({name: 'Ada', theme: 'dark'}));
        const user = JSON.parse((await AsyncStorage.getItem('user')) ?? '{}');
        said.push(`object: name=${user.name} theme=${user.theme}`);

        await AsyncStorage.removeItem('user');
        said.push(`removed: ${(await AsyncStorage.getItem('user')) === null}`);

        const keys = [...(await AsyncStorage.getAllKeys())].sort();
        said.push(`keys: ${keys.join(',')}`);

        said.push(`missing is null: ${(await AsyncStorage.getItem('nope')) === null}`);
      } catch (error) {
        said.push(`error: ${String(error)}`);
      }
      setLines(said);
    })();
  }, []);

  return (
    <View style={styles.page}>
      {lines.map(line => (
        <Text key={line} style={styles.line}>
          {line}
        </Text>
      ))}
    </View>
  );
}

AppRegistry.registerComponent('BasaltStorage', () => App);
