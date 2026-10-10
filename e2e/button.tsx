/**
 * React Native's own `<Button>`, which is the component an app reaches for
 * before it writes one of its own.
 *
 * Three of them, because the three states are three different tables in
 * upstream's stylesheet and a platform that is neither iOS nor Android got
 * nothing from any of them: a plain button, a disabled one, and one with a
 * `color`. Before 2026-10-10 the first two rendered as black centred text on
 * no background at all and were indistinguishable from each other.
 *
 * The press matters as much as the paint. `<Button>` is a `TouchableOpacity`
 * around a `<View>`, so a tap has to travel through the responder system and
 * out again, which is a different path from the `<Pressable>` the other
 * scenarios use.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Button, Platform, StyleSheet, Text, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#ffffff', padding: 24},
  row: {marginBottom: 20},
  label: {fontSize: 12, color: '#555555', marginBottom: 4},
});

function App() {
  return (
    <View style={styles.page}>
      <View style={styles.row}>
        <Text style={styles.label}>Button, enabled</Text>
        <Button title="Press me" onPress={() => console.log('button pressed')} />
      </View>
      <View style={styles.row}>
        <Text style={styles.label}>Button, disabled</Text>
        <Button
          title="Not pressable"
          disabled={true}
          onPress={() => console.log('the disabled button was pressed')}
        />
      </View>
      <View style={styles.row}>
        <Text style={styles.label}>Button, with a colour</Text>
        <Button title="Coloured" color="#8b5cf6" onPress={() => {}} />
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltButton', () => App);
