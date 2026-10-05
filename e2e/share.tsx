/**
 * A React Native app whose whole job is to call `Share.share()`.
 *
 * Worth an app of its own because the interesting part is not visible in a view
 * tree: what `Share` does on this platform is decided in three places at once,
 * and until phase 55 the first of them stopped the other two ever running.
 * React Native's `Share.js` branches on `Platform.OS` being exactly `android`
 * or `ios` and rejects with "Unsupported platform" otherwise, so no desktop
 * module was ever reached. The replacement is in
 * packages/basalt-core/src/overrides/Share.js.
 *
 * Underneath, macOS shows `NSSharingServicePicker` and the other two show a
 * small picker built from a clipboard and a mail client, because Linux has no
 * share service at all. Either way the promise settles the same three ways, and
 * that is what this logs.
 *
 * The sheet is modal and an automated run has nobody to dismiss it, so the
 * default is to check everything up to the point of opening one:
 * that the module is there, that the invariants still hold, and that a call
 * with neither a message nor a URL is refused. Pass BASALT_SHARE_OPEN=1 to
 * actually open it and watch what the promise does.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  AppRegistry,
  Clipboard,
  Platform,
  Pressable,
  Share,
  StyleSheet,
  View,
} from 'react-native';
import {messageOf} from './errors';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  button: {
    height: 72,
    borderRadius: 10,
    backgroundColor: '#4285f4',
  },
  tally: {flexDirection: 'row', marginTop: 16},
  pip: {
    width: 28,
    height: 28,
    borderRadius: 14,
    marginRight: 10,
    backgroundColor: '#56c98a',
  },
});

const CONTENT = {
  title: 'basalt-core',
  message: 'React Native on the desktop',
  url: 'https://github.com/vincentvella/basalt-core',
};

// Everything that can be checked without a person in front of the screen.
const CHECKS: Array<[string, () => Promise<boolean>]> = [
  [
    'Share exists and has the two action constants',
    async () =>
      typeof Share.share === 'function' &&
      Share.sharedAction === 'sharedAction' &&
      Share.dismissedAction === 'dismissedAction',
  ],
  [
    'sharing nothing is refused rather than shown',
    async () => {
      // React Native's own invariant, which the replacement keeps: a share
      // sheet with nothing in it is a bug in the caller, and it is better to
      // say so than to open an empty one.
      try {
        // Cast because this argument is exactly what is being asserted to be
        // refused: `ShareContent` requires a url or a message, and the check
        // is that the runtime says so too rather than opening an empty sheet.
        await Share.share({} as Parameters<typeof Share.share>[0]);
        return false;
      } catch (error) {
        return /URL or message/.test(messageOf(error));
      }
    },
  ],
  [
    'a share reaches the platform rather than being refused in JavaScript',
    async () => {
      // The one that used to fail, and the reason this app exists. "Unsupported
      // platform" is React Native's own rejection, raised before any module is
      // consulted; anything else means the call got out of JavaScript.
      //
      // Under BASALT_TEST_DIALOG the picker answers without being shown, so the
      // promise settles and the action is what it settled with. Without it a
      // real sheet opens and stays open, which is what "still open" means and
      // is the right answer for a person sitting in front of it.
      const promise = Share.share(CONTENT, {dialogTitle: 'Share this'});
      const outcome = await Promise.race([
        promise.then(
          result => `action ${result.action}`,
          error => `rejected: ${messageOf(error)}`,
        ),
        new Promise(resolve => setTimeout(() => resolve('still open'), 800)),
      ]);
      console.log(`share: ${outcome}`);

      // What the picker's "Copy" actually did. Only the desktops that use the
      // fallback picker get here -- macOS shows NSSharingServicePicker, which
      // the instrument answers without running any of this -- so it is reported
      // rather than asserted, and the end-to-end suite decides which platforms
      // it means.
      if (outcome === 'action sharedAction') {
        console.log(`clipboard: ${await Clipboard.getString()}`);
      }
      return outcome !== 'rejected: Unsupported platform';
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
      console.log(`share checks: ${count}/${CHECKS.length}`);
      setPassed(count);
    })();
  }, []);

  return (
    <View style={styles.page}>
      <Pressable
        accessible={true}
        accessibilityRole="button"
        accessibilityLabel="Share this app"
        style={styles.button}
        onPress={async () => {
          const result = await Share.share(CONTENT, {dialogTitle: 'Share this'});
          console.log(`share: ${result.action}`);
        }}
      />
      <View style={styles.tally}>
        {Array.from({length: passed}, (_, index) => (
          <View key={index} style={styles.pip} />
        ))}
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltShare', () => App);
