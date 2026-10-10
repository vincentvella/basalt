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
  // textDecorationColor and textDecorationStyle, which the hosts read as of
  // 2026-10-09. No dump line shows a decoration -- it is per fragment and the
  // dump is per view -- so what asserts these is the pixel and attribute tests
  // in each host's suite; these are here so both hosts actually draw them.
  decorated: {
    fontSize: 16,
    color: '#2b3445',
    textDecorationLine: 'underline',
    textDecorationColor: '#16a34a',
    textDecorationStyle: 'double',
  },
  // fontVariant and a fragment's opacity, both read as of 2026-10-09. Whether
  // small caps changes the glyphs depends on the font having the table, so what
  // the hosts' own suites assert is the feature reaching the engine; these are
  // here so both hosts ask for it on a real paragraph.
  // writingDirection, which decides which edge a paragraph starts from. Latin
  // text in a right-to-left paragraph is the case worth having: the box and the
  // string are identical, so only the pixels differ, which is why each host's
  // suite asserts this with a picture and the dump only reports what was asked.
  rtl: {fontSize: 16, color: '#2b3445', writingDirection: 'rtl'},
  // `textAlign`'s two relative spellings, which name the edges a line runs
  // between rather than the edges of the box: `end` in a right-to-left
  // paragraph is the *left* one, and `left` is still the left one. Four
  // paragraphs, because the direction can come from the prop or from the text
  // and the answer has to be the same either way -- which it was not: all three
  // hosts had `end` flush right, and each had a different second row wrong.
  // The dump's `text-align=` line is the resolved edge, so these are comparable
  // across hosts; each host's own suite asserts the pixels.
  endLtr: {fontSize: 16, color: '#2b3445', textAlign: 'end'},
  endRtl: {fontSize: 16, color: '#2b3445', textAlign: 'end', writingDirection: 'rtl'},
  leftRtl: {fontSize: 16, color: '#2b3445', textAlign: 'left', writingDirection: 'rtl'},
  // No alignment and no direction: both are resolved from the text, which is
  // Hebrew, so this is a right-to-left paragraph flush right. Whether the
  // machine has a font for it does not enter into it.
  hebrew: {fontSize: 16, color: '#2b3445'},
  // `verticalAlign`, the cross-platform spelling, and `textAlignVertical`, the
  // Android one. The first is rewritten into the second by React Native's own
  // `Text.js` -- `middle` becoming `center` -- so the dump's `text-valign=`
  // line is the proof that the chain from a stylesheet to a mounting manager
  // works, which no unit test can show: they all build the attributes directly.
  //
  // A height of its own, because the prop only means anything in a box taller
  // than the text: a paragraph Yoga sized to its content has no slack to sit in.
  middled: {fontSize: 16, color: '#2b3445', height: 60, verticalAlign: 'middle'},
  grounded: {fontSize: 16, color: '#2b3445', height: 60, textAlignVertical: 'bottom'},
  // Padding on the <Text> itself rather than on a <View> around it, which is
  // what React Native's own LogBox writes and what every host drew as if it
  // were zero: the box was laid out with it and the text was painted in the
  // corner, underneath its own padding.
  padded: {
    fontSize: 16,
    color: '#2b3445',
    paddingHorizontal: 12,
    paddingVertical: 8,
    backgroundColor: '#e8ecf5',
  },
  variants: {fontSize: 16, color: '#2b3445', fontVariant: ['small-caps', 'tabular-nums']},
  faded: {fontSize: 16, color: '#2b3445', opacity: 0.4},
  dotted: {
    fontSize: 16,
    color: '#2b3445',
    textDecorationLine: 'underline line-through',
    textDecorationColor: '#d97706',
    textDecorationStyle: 'dotted',
  },
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
  // allowFontScaling and maxFontSizeMultiplier, which only mean anything when
  // something is scaling: BASALT_TEST_FONT_SCALE supplies that, because two of
  // the three desktops publish no text scale of their own. See
  // core/FontScaling.h.
  scaling: {fontSize: 16, color: '#2b3445'},
  fixed: {fontSize: 16, color: '#2b3445'},
  capped: {fontSize: 16, color: '#2b3445'},
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
        <Text style={styles.scaling}>Scales with the desktop</Text>
        <Text style={styles.fixed} allowFontScaling={false}>
          Fixed whatever the desktop says
        </Text>
        <Text style={styles.capped} maxFontSizeMultiplier={1.25}>
          Capped at a quarter larger
        </Text>
      </View>

      <View style={styles.card}>
        <Text style={styles.underlined}>Underlined</Text>
        <Text style={styles.struck}>Struck through</Text>
        <Text style={styles.decorated}>Double, in its own colour</Text>
        <Text style={styles.dotted}>Dotted both ways</Text>
        <Text style={styles.variants}>Small caps 1234567890</Text>
        <Text style={styles.faded}>Faded to two fifths</Text>
        <Text style={styles.rtl}>Right to left, in Latin</Text>
        <Text style={styles.endLtr}>Ends at the right</Text>
        <Text style={styles.endRtl}>Ends at the left</Text>
        <Text style={styles.leftRtl}>Left is still left</Text>
        <Text style={styles.hebrew}>{'\u05e9\u05dc\u05d5\u05dd \u05e2\u05d5\u05dc\u05dd'}</Text>
        <Text style={styles.middled}>Halfway down its box</Text>
        <Text style={styles.grounded}>At the bottom of its box</Text>
        <Text style={styles.padded}>Inside its own padding</Text>
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
