/**
 * The events an `<Image>` fires while it loads.
 *
 * Separate from image.tsx, which is about what gets painted, because this one
 * needs something painting cannot have: a server that answers slowly. The
 * harness starts one on port 8097 and dribbles a few hundred kilobytes out in
 * chunks, so `onProgress` has something to report more than once -- a local
 * file arrives whole and would fire a single event at a hundred percent, which
 * would pass a test that a one-line stub also passes.
 *
 * The URL is fixed rather than handed over at runtime. A bundle has no way to
 * read the harness's environment, and the alternative -- an ephemeral port
 * written into a file the app then reads -- would need a module to read it
 * with. The port is checked before the server starts, so a stranger listening
 * on it is a failure with a message rather than a mystery.
 *
 * Five events, and all five were missing somewhere when this was written:
 * `onProgress` on every host, and `onLoadStart`, `onLoad`, `onError` and
 * `onLoadEnd` on Windows, which dispatched none of them.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Image, Platform, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

// The harness serves this, slowly. See `ImageServer` in
// scripts/integration_test.py.
const SLOW = {uri: 'http://127.0.0.1:8097/slow.png'};

// A file that is not there, for the other half of the family: a reported error
// rather than a blank box is the difference between "this image is missing" and
// "images are broken".
const MISSING = {uri: 'assets/there-is-no-such-file.png'};

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#1f2129', padding: 24},
  cell: {width: 180, height: 120, marginBottom: 16, backgroundColor: '#2b3445'},
});

function App() {
  return (
    <View style={styles.page}>
      <Image
        source={SLOW}
        resizeMode="cover"
        style={styles.cell}
        onLoadStart={() => console.log('slow image: load start')}
        onProgress={event => {
          const {loaded, total} = event.nativeEvent;
          console.log(`slow image: progress ${loaded}/${total}`);
        }}
        /* Nothing emits this on any desktop: iOS fires it for a progressive
           JPEG, which is a decode that can draw a partial image, and neither
           gdk-pixbuf's one-shot loader, ImageIO's `CGImageSourceCreateImage`
           nor WIC's decoder is being driven incrementally here. Listened for
           anyway, so the day one of them is, the scenario says so. */
        onPartialLoad={() => console.log('slow image: partial load')}
        onLoad={event => {
          const {width, height} = event.nativeEvent.source;
          console.log(`slow image: loaded ${width}x${height}`);
        }}
        onError={event => console.log(`slow image: error ${event.nativeEvent.error}`)}
        onLoadEnd={() => console.log('slow image: load end')}
      />
      <Image
        source={MISSING}
        style={styles.cell}
        onError={event => console.log(`missing image: error ${event.nativeEvent.error}`)}
        onLoad={() => console.log('missing image: loaded, which is the bug')}
        onLoadEnd={() => console.log('missing image: load end')}
      />
    </View>
  );
}

AppRegistry.registerComponent('BasaltImageLoad', () => App);
