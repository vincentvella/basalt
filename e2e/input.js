/**
 * A React Native app built around `<TextInput>`.
 *
 * The hard part of a text field is not typing, it is that `<TextInput>` is a
 * controlled component: JavaScript owns the value, the field reports every
 * change, JavaScript re-renders and sends the value back down. If applying that
 * prop looks like the user typing, the two chase each other forever.
 *
 * So the first field is controlled and upper-cases what it is given, which is
 * the case that proves the loop is broken correctly: the text the user typed is
 * never what comes back, so a field that ignored the prop and a field that
 * fought it both look wrong immediately.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, TextInput, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#f6f7f9', padding: 24},
  label: {fontSize: 14, color: '#6b7280', marginBottom: 6},
  field: {
    height: 44,
    marginBottom: 20,
    paddingHorizontal: 12,
    backgroundColor: '#ffffff',
    borderRadius: 8,
    fontSize: 16,
    color: '#1f2129',
  },
  echo: {fontSize: 16, color: '#1f2129'},
});

function App() {
  const [shouted, setShouted] = React.useState('');
  const [free, setFree] = React.useState('');

  return (
    <View style={styles.page}>
      <Text style={styles.label}>Controlled, and upper-cased on the way back</Text>
      <TextInput
        style={styles.field}
        value={shouted}
        placeholder="type here"
        onChangeText={text => {
          setShouted(text.toUpperCase());
          console.log(`changed: ${text}`);
        }}
        onFocus={() => console.log('focused')}
        onBlur={() => console.log('blurred')}
        onSubmitEditing={() => console.log('submitted')}
      />

      <Text style={styles.label}>Uncontrolled, with a secure twin below</Text>
      <TextInput style={styles.field} placeholder="anything" onChangeText={setFree} />
      <TextInput style={styles.field} placeholder="secret" secureTextEntry={true} />

      <Text style={styles.echo}>{shouted === '' ? '(nothing yet)' : shouted}</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltInput', () => App);
