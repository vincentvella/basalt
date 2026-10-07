/**
 * The clipboard, which ships on all three hosts and had never been tested.
 *
 * It became load-bearing before it was covered: a menu role's behaviour was
 * asserted by seeding the clipboard and pasting, and the clipboard turned out not
 * to round-trip under the display CI runs on. A module nothing exercises is a
 * module nobody knows the state of, so this exercises it.
 *
 * The second thing this is for does not appear in the checks at all. A host was
 * once seen to log `BASALT_QUIT_AFTER_MS elapsed; quitting` after a run that read
 * the clipboard, and then not exit. Every run of this app reads it and then quits,
 * so the scenario asserting that the host goes away is the reproduction attempt.
 * See docs/backlog/testing.md.
 *
 * Results are logged and rendered as a row of boxes, one per check that passed, so
 * the tree says how many without needing a text engine.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Clipboard, Platform, StyleSheet, View} from 'react-native';
import {messageOf} from './errors';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24, flexDirection: 'row'},
  pip: {width: 40, height: 40, borderRadius: 20, marginRight: 12, backgroundColor: '#56c98a'},
});

// Each returns a promise of true. `getString` is asynchronous on every host, so
// every check awaits it rather than assuming the write is visible immediately.
const CHECKS: Array<[string, () => Promise<boolean>]> = [
  [
    'a string set is a string read back',
    async () => {
      Clipboard.setString('basalt clipboard');
      return (await Clipboard.getString()) === 'basalt clipboard';
    },
  ],

  [
    'the second write wins',
    async () => {
      Clipboard.setString('first');
      Clipboard.setString('second');
      return (await Clipboard.getString()) === 'second';
    },
  ],

  [
    'an empty string clears it rather than being ignored',
    async () => {
      Clipboard.setString('something');
      Clipboard.setString('');
      return (await Clipboard.getString()) === '';
    },
  ],

  [
    'text outside ASCII survives the trip',
    async () => {
      // A clipboard that goes through the platform as bytes is a clipboard that
      // can mangle these, and each of the three has its own encoding to get wrong.
      const text = 'grüße ☃ 😀 日本語';
      Clipboard.setString(text);
      return (await Clipboard.getString()) === text;
    },
  ],

  [
    'a long string is not truncated',
    async () => {
      // 64KB. X11 hands a selection over in chunks above a threshold, and the
      // chunked path is the one that is never taken by a short string.
      const text = 'x'.repeat(64 * 1024);
      Clipboard.setString(text);
      const back = await Clipboard.getString();
      return back.length === text.length;
    },
  ],

  [
    'newlines and tabs are kept as they were',
    async () => {
      const text = 'one\ttwo\nthree\r\nfour';
      Clipboard.setString(text);
      return (await Clipboard.getString()) === text;
    },
  ],

  [
    'getString answers a string rather than null',
    async () => {
      // What an app checking `.length` depends on, and the kind of thing a native
      // module returns as undefined when the platform has nothing to give.
      Clipboard.setString('');
      const back = await Clipboard.getString();
      return typeof back === 'string';
    },
  ],
];

function App() {
  const [passed, setPassed] = React.useState(0);

  React.useEffect(() => {
    (async () => {
      let count = 0;
      for (const [name, check] of CHECKS) {
        try {
          const ok = await check();
          console.log(`${ok ? 'pass' : 'FAIL'}: ${name}`);
          if (ok) {
            count++;
          }
        } catch (error) {
          console.log(`FAIL: ${name} threw ${messageOf(error)}`);
        }
      }
      console.log(`clipboard checks: ${count}/${CHECKS.length}`);
      setPassed(count);
    })();
  }, []);

  return (
    <View style={styles.page}>
      {Array.from({length: passed}, (_, index) => (
        <View key={index} style={styles.pip} />
      ))}
    </View>
  );
}

AppRegistry.registerComponent('BasaltClipboard', () => App);
