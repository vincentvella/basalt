/**
 * A React Native app that is mostly `<Text>`.
 *
 * The counterpart of e2e/views.js for the text milestone: it exercises the
 * attributes that a text engine has to get right and that differ between
 * engines -- size, weight, colour, alignment, line height, letter spacing,
 * decorations, nested fragments with their own styles, and numberOfLines with
 * an ellipsis.
 *
 * Nothing branches on the platform, because `scripts/compare_hosts.sh` diffs
 * the two trees. Text is where the two are least likely to agree exactly: Pango
 * and Core Text are different shapers over different system fonts, so the
 * *strings* must match and the measured sizes will not. That difference is the
 * point of running it on both.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#f6f7f9', padding: 24},
  card: {backgroundColor: '#ffffff', borderRadius: 8, padding: 16, marginBottom: 16},
  heading: {fontSize: 28, fontWeight: '700', color: '#1f2129'},
  subtle: {fontSize: 14, color: '#6b7280', marginTop: 4},
  centered: {fontSize: 16, textAlign: 'center', color: '#2b3445'},
  right: {fontSize: 16, textAlign: 'right', color: '#2b3445'},
  spaced: {fontSize: 16, letterSpacing: 2, color: '#2b3445'},
  tall: {fontSize: 16, lineHeight: 32, color: '#2b3445'},
  underlined: {fontSize: 16, textDecorationLine: 'underline', color: '#2563eb'},
  struck: {fontSize: 16, textDecorationLine: 'line-through', color: '#b91c1c'},
  clipped: {fontSize: 16, color: '#2b3445'},
  // textTransform, which changes the string the engine lays out rather than how
  // it is drawn: the tree dump shows the transformed text, so both hosts can be
  // asked whether they transformed it and whether they agree.
  shouted: {fontSize: 16, textTransform: 'uppercase', color: '#2b3445'},
  titled: {fontSize: 16, textTransform: 'capitalize', color: '#2b3445'},
  // textShadow*, three props that no host read: every pre-CSS React Native
  // title sets them. The radius is a standard deviation, which is what iOS puts
  // into NSShadow and what the dump prints, and the offset's sign is what says
  // the shadow went down the screen rather than up.
  shadowed: {
    fontSize: 16,
    color: '#2b3445',
    textShadowColor: '#4d8cf2',
    textShadowOffset: {width: 2, height: 3},
    textShadowRadius: 4,
  },
  emphasis: {fontWeight: '700', color: '#b45309'},
  italic: {fontStyle: 'italic', color: '#047857'},
});

const LONG =
  'A paragraph long enough to wrap several times over, so that the line ' +
  'breaking has real work to do and numberOfLines has something to cut: this ' +
  'sentence keeps going well past two lines so the ellipsis has to appear, ' +
  'and if it does not, the truncation path is broken rather than untested.';

function App() {
  return (
    <View style={styles.page}>
      <View style={styles.card}>
        <Text style={styles.heading}>Text on the desktop</Text>
        <Text style={styles.subtle}>Pango on Linux, Core Text on macOS.</Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.centered}>Centred</Text>
        <Text style={styles.right}>Right aligned</Text>
        <Text style={styles.spaced}>Letter spaced</Text>
        <Text style={styles.tall}>Line height thirty two</Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.shouted}>shout quietly</Text>
        <Text style={styles.titled}>iOS and android</Text>
        <Text style={styles.shadowed}>Shadowed</Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.underlined}>Underlined</Text>
        <Text style={styles.struck}>Struck through</Text>
        <Text style={styles.clipped}>
          Plain, then <Text style={styles.emphasis}>bold amber</Text> and{' '}
          <Text style={styles.italic}>italic green</Text> in one paragraph.
        </Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.clipped} numberOfLines={2}>
          {LONG}
        </Text>
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltText', () => App);
