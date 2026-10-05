/**
 * A React Native app that can be driven entirely from the keyboard.
 *
 * Tab reaching a `<Pressable>` is not something React Native gives a platform
 * for free. There is a `focusable` prop and it never arrives: ReactCommon
 * parses it only into Android's and tvOS's props, and the C++ host's are a bare
 * alias of the base ones. What does arrive is `accessible` -- which `<Pressable>`
 * sets on everything it renders -- so that is what makes a view a Tab stop
 * here, and it is also what a screen reader stops on.
 *
 * Three buttons and a text field, in that order, because the field is the
 * interesting neighbour: it takes focus because it is a real GtkText or
 * NSTextField, and it has to sit in the same Tab order as the buttons around it
 * without either side knowing about the other.
 *
 * Every focus, blur and press is logged, which is what the end-to-end suite
 * reads under BASALT_TEST_FOCUS. Pressing a focused button goes through
 * `onPress` and nothing else: activating from the keyboard dispatches the same
 * `topClick` React Native for Android dispatches, and Pressability turns it
 * into the press an app already handles.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  AppRegistry,
  Platform,
  Pressable,
  StyleSheet,
  TextInput,
  View,
} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const BUTTONS = ['first', 'second', 'third'];

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  button: {
    height: 64,
    marginBottom: 16,
    borderRadius: 10,
    backgroundColor: '#4285f4',
  },
  buttonFocused: {backgroundColor: '#8ab4f8'},
  field: {
    height: 44,
    marginBottom: 16,
    paddingHorizontal: 12,
    borderRadius: 8,
    borderWidth: 2,
    borderColor: '#2b3140',
    backgroundColor: '#1a1e28',
    fontSize: 17,
    color: '#f7f8fa',
  },
  tally: {flexDirection: 'row'},
  pip: {
    width: 28,
    height: 28,
    borderRadius: 14,
    marginRight: 10,
    backgroundColor: '#56c98a',
  },
});

function Button({name, onPress}) {
  const [focused, setFocused] = React.useState(false);

  return (
    <Pressable
      accessible={true}
      accessibilityRole="button"
      accessibilityLabel={`${name} button`}
      style={[styles.button, focused && styles.buttonFocused]}
      onFocus={() => {
        setFocused(true);
        console.log(`focus ${name}`);
      }}
      onBlur={() => {
        setFocused(false);
        console.log(`blur ${name}`);
      }}
      onPress={() => onPress(name)}
    />
  );
}

function App() {
  // Counted as well as logged, so the same file proves the same thing on a
  // platform with no text engine: one pip per press that reached React.
  const [count, setCount] = React.useState(0);

  const onPress = React.useCallback(name => {
    console.log(`press ${name}`);
    setCount(previous => previous + 1);
  }, []);

  return (
    <View style={styles.page}>
      {BUTTONS.map(name => (
        <Button key={name} name={name} onPress={onPress} />
      ))}
      <TextInput
        style={styles.field}
        placeholder="a field in the same Tab order"
        accessibilityLabel="Notes"
        onFocus={() => console.log('focus field')}
        onBlur={() => console.log('blur field')}
      />
      <View style={styles.tally}>
        {Array.from({length: Math.min(count, 12)}, (_, index) => (
          <View key={index} style={styles.pip} />
        ))}
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltFocus', () => App);
