/**
 * A React Native app that loads a font while it is running.
 *
 * This is the whole of `expo-font` as a non-web platform sees it:
 * `ExpoFontLoader.loadAsync(name, file)`, then `fontFamily: name` working
 * afterwards. The name is the app's own choice and the file calls itself
 * something else, so the seam has two halves -- registering the file with the
 * platform's font system, and remembering the mapping -- and both are in
 * native/core/FontRegistry.h.
 *
 * The loader is reached through `expo.modules` rather than by importing
 * `expo-font`, which this directory installs nothing to import. The JavaScript
 * package adds `useFonts` and asset resolution over exactly this call, so what
 * is skipped is Expo's own code and what is covered is every line of ours.
 *
 * The font is a monospaced one off the machine, by path. A proportional face
 * would be the wrong instrument: the check is that the second paragraph
 * measures differently from the first, and two proportional faces can measure a
 * string to the same width. Which paths exist is a property of the operating
 * system, so several are tried and the one that worked is logged; a machine
 * with none of them makes the scenario skip rather than fail.
 *
 * **The load deliberately happens after the first render.** That is how every
 * loader works, `useFonts` included: the app draws in the fallback, the font
 * arrives, and the paragraph has to be measured again. A measurement cached
 * from before is the bug this shape exists to catch, and the hosts drop that
 * cache when a font is registered.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';
import {messageOf} from './errors';

// What the app calls the font. Nothing on any machine is called this, which is
// the point: if the paragraph below renders in it, the mapping was remembered.
const FAMILY = 'BasaltRuntimeFont';

// The same string in both paragraphs, so the only thing that can make their
// widths differ is the font.
const SPECIMEN = 'Handgloves 0123';

const CANDIDATES: {[key: string]: Array<string>} = {
  macos: [
    '/System/Library/Fonts/Supplemental/Courier New.ttf',
    '/System/Library/Fonts/Supplemental/Andale Mono.ttf',
    '/System/Library/Fonts/Monaco.ttf',
  ],
  linux: [
    '/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf',
    '/usr/share/fonts/truetype/liberation/LiberationMono-Regular.ttf',
    '/usr/share/fonts/truetype/liberation2/LiberationMono-Regular.ttf',
    '/usr/share/fonts/TTF/DejaVuSansMono.ttf',
    '/usr/share/fonts/dejavu/DejaVuSansMono.ttf',
    '/usr/share/fonts/liberation/LiberationMono-Regular.ttf',
  ],
  windows: ['C:\\Windows\\Fonts\\consola.ttf'],
};

type FontLoader = {
  loadAsync: (name: string, file: string) => Promise<void>;
  isLoaded: (name: string) => boolean;
  getLoadedFonts: () => Array<string>;
};

function loader(): FontLoader | null {
  const expo = (globalThis as {expo?: {modules?: {ExpoFontLoader?: FontLoader}}}).expo;
  return expo?.modules?.ExpoFontLoader ?? null;
}

const styles = StyleSheet.create({
  page: {flex: 1, padding: 24, backgroundColor: '#ffffff'},
  // `alignSelf` is load-bearing. A <Text> in a column stretches to the page's
  // width, and its frame is then the box it was given rather than the width it
  // measured, which is the number this app exists to make differ. The first
  // version of this file left it out and both paragraphs reported 852.
  line: {fontSize: 28, color: '#111111', alignSelf: 'flex-start'},
  loaded: {fontSize: 28, color: '#111111', alignSelf: 'flex-start', fontFamily: FAMILY},
});

function App() {
  const [loaded, setLoaded] = React.useState(false);

  React.useEffect(() => {
    const fonts = loader();
    if (fonts == null) {
      console.log('font: no ExpoFontLoader');
      return;
    }

    (async () => {
      for (const path of CANDIDATES[Platform.OS] ?? []) {
        try {
          await fonts.loadAsync(FAMILY, path);
          console.log(`font: loaded ${path}`);
          console.log(`font: isLoaded ${fonts.isLoaded(FAMILY)}`);
          console.log(`font: names ${fonts.getLoadedFonts().join(',')}`);
          setLoaded(true);
          return;
        } catch (error) {
          // Every candidate that is not on this machine lands here, which is
          // the normal case for all but one of them.
          console.log(`font: ${path} did not load: ${messageOf(error)}`);
        }
      }
      console.log('font: none of the candidates loaded');
    })();
  }, []);

  return (
    <View style={styles.page}>
      {/* The fallback, and the measuring stick: whatever this host's default
          font is, at the same size and with the same string. */}
      <Text style={styles.line}>{SPECIMEN}</Text>
      {/* The same string in the font the app loaded. Rendered only once the
          load has resolved, so this paragraph is measured after the
          registration rather than before it -- the tree would otherwise
          contain a paragraph that asked for a font nobody had yet, which is
          indistinguishable from one that asked and was ignored. */}
      {loaded ? <Text style={styles.loaded}>{SPECIMEN}</Text> : null}
    </View>
  );
}

AppRegistry.registerComponent('BasaltFonts', () => App);
