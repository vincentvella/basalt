/**
 * An inline `<View>` inside a `<Text>`.
 *
 * React Native represents one as a fragment holding U+FFFC whose size it has
 * measured from the view's own layout, and the text engine has to reserve a box
 * for it and report where that box ended up. Pango does it with a shape
 * attribute and Core Text with a run delegate; both used to do neither, so a
 * view inside a sentence reserved a missing glyph's width and was positioned at
 * the origin. See backlog/text.md.
 *
 * Three cases, each a separate line of the tree so a failure names one:
 *
 *   mid      a view between two runs of text, which must sit after the first
 *   tall     a view taller than the line, which must make the line taller
 *   leading  a view before any text, which must sit at the left edge
 *
 * The sizes are deliberately round numbers and larger than any glyph, so a frame
 * that came from the font rather than from the view is obvious in the tree.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#ffffff', padding: 20},
  line: {fontSize: 16, color: '#111827', marginBottom: 12},
  mid: {width: 48, height: 24, backgroundColor: '#2f6fed'},
  tall: {width: 24, height: 64, backgroundColor: '#e0484d'},
  leading: {width: 32, height: 16, backgroundColor: '#1f9d55'},
});

function App() {
  return (
    <View style={styles.page}>
      <Text style={styles.line}>
        before <View style={styles.mid} /> after
      </Text>
      <Text style={styles.line}>
        short <View style={styles.tall} /> tall
      </Text>
      <Text style={styles.line}>
        <View style={styles.leading} /> trailing text
      </Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltInline', () => App);
