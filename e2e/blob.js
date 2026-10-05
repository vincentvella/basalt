/**
 * A React Native app exercising `Blob`, `File` and `FileReader`.
 *
 * These are web APIs React Native implements on top of a native byte store:
 * JavaScript holds `{blobId, offset, size}` and the bytes never enter its heap,
 * which is the whole point and also why none of it works without a native
 * BlobModule. A stock Expo app asks for one during startup.
 *
 * Every result is logged rather than rendered, and the rendered tree is a row
 * of boxes -- one per check that passed. So the tree says how many passed
 * without depending on a text engine, and the log says which.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#1f2129', padding: 24, flexDirection: 'row'},
  pip: {width: 40, height: 40, borderRadius: 20, marginRight: 12, backgroundColor: '#59cc8c'},
});

// Each returns a promise of true. Deliberately small and specific, so a failure
// names one thing.
//
// Named in data rather than by `Function.name`: the bundler minifies names
// away, and the first run of this file reported ten passes with no names at
// all, which would have been useless had any of them failed.
// Each returns a promise of true. Deliberately small and specific, so a failure
// names one thing.
//
// Named in data rather than by `Function.name`: the bundler minifies names
// away, and the first run of this file reported ten passes with no names at
// all, which would have been useless had any of them failed.
const CHECKS = [
  ['blob from a string', async () => new Blob(['hello']).size === 5],

  ['blob from several parts', async () => new Blob(['abc', 'de']).size === 5],

  [
    'blob from a blob',
    async () => new Blob(['hello ', new Blob(['world'])]).size === 11,
  ],

  ['slicing a blob', async () => new Blob(['0123456789']).slice(2, 5).size === 3],

  [
    'readAsText',
    async () => (await readBlob('Text', new Blob(['round trip']))) === 'round trip',
  ],

  [
    'readAsText of a slice',
    async () =>
      (await readBlob('Text', new Blob(['0123456789']).slice(2, 5))) === '234',
  ],

  [
    'readAsDataURL',
    async () =>
      // "hi" is aGk= in base64, and the padding is the half most likely wrong.
      (await readBlob('DataURL', new Blob(['hi'], {type: 'text/plain'}))) ===
      'data:text/plain;base64,aGk=',
  ],

  [
    'base64 padding, all three tails',
    async () => {
      // Lengths 1, 2 and 3 take all three paths through the tail of a base64
      // encoder, which is where that algorithm is always wrong if it is wrong.
      const one = await readBlob('DataURL', new Blob(['a']));
      const two = await readBlob('DataURL', new Blob(['ab']));
      const three = await readBlob('DataURL', new Blob(['abc']));
      return one.endsWith('YQ==') && two.endsWith('YWI=') && three.endsWith('YWJj');
    },
  ],

  [
    'a File has a name and a type',
    async () => {
      const file = new File(['x'], 'note.txt', {type: 'text/plain'});
      return file.name === 'note.txt' && file.type === 'text/plain' && file.size === 1;
    },
  ],

  [
    'URL.createObjectURL',
    async () => {
      const url = URL.createObjectURL(new Blob(['x']));
      return typeof url === 'string' && url.startsWith('blob:');
    },
  ],

  [
    'a released blob reads as an error, not as empty',
    async () => {
      const blob = new Blob(['gone']);
      blob.close();
      try {
        await readBlob('Text', blob);
        return false;
      } catch {
        // Rejecting is the point: a released blob that read as "" would be a
        // silent wrong answer rather than a loud one.
        return true;
      }
    },
  ],
];

// Not in CHECKS: this one is expected to fail, and the point is to find out
// *how*. ReactCxxPlatform types `http::Body::blob` as a string and JavaScript
// sends `{blobId, offset, size}`, so a Blob request body cannot reach the http
// client at all. Running it says whether that is a throw or a silent empty
// body, which is the difference between an app that breaks loudly and one that
// uploads nothing.
async function probeBlobUpload() {
  try {
    // Nothing listens on this port. The request never has to succeed -- what is
    // being probed is whether the body survives the trip to native.
    await fetch('http://127.0.0.1:9/upload', {
      method: 'POST',
      body: new Blob(['payload']),
    });
    console.log('probe: blob upload reached the network layer');
  } catch (error) {
    console.log(`probe: blob upload threw ${error.message}`);
  }
}

// FileReader is callback-shaped; every check above wants a promise.
function readBlob(how, blob) {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(reader.result);
    reader.onerror = () => reject(reader.error ?? new Error('read failed'));
    reader[`readAs${how}`](blob);
  });
}

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
          console.log(`FAIL: ${name} threw ${error.message}`);
        }
      }
      console.log(`blob checks: ${count}/${CHECKS.length}`);
      await probeBlobUpload();
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

AppRegistry.registerComponent('BasaltBlob', () => App);
