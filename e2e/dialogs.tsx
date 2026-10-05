/**
 * A React Native app about the native file dialogs.
 *
 * React Native has no API for these, because a phone has none, so this is the
 * first screen in this repository that is about something React Native does not
 * do at all rather than about something it does elsewhere.
 *
 * Three buttons, one per kind, each logging what came back. What is worth
 * asserting is the shape rather than the pixels: every one answers
 * `{canceled, paths}`, `canceled` is a different answer from an empty list, and
 * a save's single path arrives in the same list a multiple open's several do.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Pressable, StyleSheet, Text, View} from 'react-native';
import {useDialog} from 'basalt-core';
import type {DialogResult} from 'basalt-core';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  // A fixed height, so the rows below sit at the same y on all three hosts:
  // text measurement is the one thing Pango, Core Text and DirectWrite will
  // never agree on, and the integration test presses fixed coordinates.
  label: {color: '#aab', fontSize: 13, height: 22},
  button: {
    width: 220,
    height: 48,
    borderRadius: 8,
    marginBottom: 12,
    backgroundColor: '#4285f4',
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: {color: '#fff', fontSize: 15},
  result: {color: '#dde', fontSize: 13, height: 40},
});

function App() {
  const dialog = useDialog();
  const [result, setResult] = React.useState('nothing yet');

  const report = React.useCallback((what: string, answer: DialogResult) => {
    // One line, so the test reads it rather than the screen.
    console.log(
      `dialog ${what}: canceled=${answer.canceled} paths=${answer.paths.join('|')}`,
    );
    setResult(`${what}: ${answer.canceled ? 'canceled' : answer.paths.join(', ')}`);
  }, []);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>File dialogs</Text>

      <Pressable
        style={styles.button}
        onPress={async () => {
          report(
            'open',
            await dialog.openFile({
              title: 'Choose an image',
              multiple: true,
              filters: [{name: 'Images', extensions: ['png', 'jpg']}],
            }),
          );
        }}>
        <Text style={styles.buttonText}>Open files…</Text>
      </Pressable>

      <Pressable
        style={styles.button}
        onPress={async () => {
          report('save', await dialog.saveFile({defaultPath: 'notes.md'}));
        }}>
        <Text style={styles.buttonText}>Save as…</Text>
      </Pressable>

      <Pressable
        style={styles.button}
        onPress={async () => {
          report('folder', await dialog.openFolder({title: 'Choose a folder'}));
        }}>
        <Text style={styles.buttonText}>Choose a folder…</Text>
      </Pressable>

      <Text style={styles.result}>{result}</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltDialogs', () => App);
