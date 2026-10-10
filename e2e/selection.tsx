/**
 * `<Text selectable>`: selecting a paragraph's text with a pointer.
 *
 * The prop did nothing on any host until 2026-10-10 -- not for want of a field,
 * which `BaseParagraphProps::isSelectable` is and which `userSelect` also
 * arrives in, but because no host drew a selection or reported one. See
 * core/TextSelection.h for what a press and a drag now mean, and
 * docs/backlog/text.md for the entry.
 *
 * What the scenario drives is `BASALT_TEST_DRAG`, which synthesises a press, a
 * run of moves and a release: a selection cannot be exercised by a tap, a sweep
 * being the whole of what makes one. The dump then carries two lines nothing
 * else can show -- `selectable`, which changes no box, and `selection=`, the
 * highlight being a wash under the glyphs.
 *
 * Two paragraphs, at absolute positions so the drag knows where to land: one
 * selectable and one not. The second is the control, and it is the half that
 * would catch a host selecting everything it can rather than what the app
 * allowed.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#f6f7f9'},
  // Absolute, so a drag's coordinates are this file's decision rather than the
  // window's. 24pt text, which is tall enough that a drag along y=70 is inside
  // the line on every font these three hosts have.
  selectable: {
    position: 'absolute',
    left: 40,
    top: 56,
    width: 420,
    fontSize: 24,
    color: '#2b3445',
  },
  plain: {
    position: 'absolute',
    left: 40,
    top: 140,
    width: 420,
    fontSize: 24,
    color: '#2b3445',
  },
});

function App() {
  return (
    <View style={styles.page}>
      <Text selectable style={styles.selectable}>
        Select this sentence
      </Text>
      <Text style={styles.plain}>Leave this one alone</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltSelection', () => App);
