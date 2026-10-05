/**
 * A React Native app about what a press can land on rather than what is drawn.
 *
 * `pointerEvents` is the only view prop that changes nothing on screen, which
 * makes it the easiest one to implement wrongly and never notice. Four rows,
 * one per value, each a panel with a button inside it and a backdrop behind it;
 * every one of the three logs which view it thinks was pressed.
 *
 * What each row should report when its *panel* is pressed, away from the button:
 *
 *   auto      the panel. The ordinary case.
 *   none      the backdrop. The panel and its button are out of hit testing
 *             entirely, so the press reaches what is behind them.
 *   box-none  the backdrop. The panel is transparent to a press that misses its
 *             children -- and pressing the button still reports the button.
 *   box-only  the panel, pressed anywhere, including over the button.
 *
 * The last two are the pair everyone gets the wrong way round, and the
 * difference between them is only visible when there is something underneath,
 * which is why the backdrop is here.
 *
 * The tree itself is deterministic -- nothing changes with the pointer -- so
 * scripts/compare_all.sh can diff it across hosts, and the `pe=` field in each
 * host's dump is what says the prop arrived at all three.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Pressable, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const MODES = ['auto', 'none', 'box-none', 'box-only'];

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  row: {height: 110, marginBottom: 12},
  // `absoluteFill` rather than `absoluteFillObject`: the second was removed
  // from React Native and is now only mentioned in StyleSheet.js's own doc
  // comment, so spreading it adds nothing and does so silently.
  backdrop: {
    ...StyleSheet.absoluteFill,
    borderRadius: 12,
    backgroundColor: '#2b3140',
  },
  panel: {
    ...StyleSheet.absoluteFill,
    margin: 14,
    borderRadius: 10,
    justifyContent: 'center',
    backgroundColor: '#4285f4',
  },
  button: {
    width: 120,
    height: 52,
    marginLeft: 16,
    borderRadius: 8,
    backgroundColor: '#f26f56',
  },
  tally: {flexDirection: 'row', marginTop: 8},
  pip: {
    width: 24,
    height: 24,
    borderRadius: 12,
    marginRight: 8,
    backgroundColor: '#56c98a',
  },
});

function Row({mode, onPress}) {
  return (
    <View style={styles.row}>
      <Pressable
        style={styles.backdrop}
        onPress={() => onPress(`${mode}: backdrop`)}
      />
      <Pressable
        style={styles.panel}
        pointerEvents={mode}
        onPress={() => onPress(`${mode}: panel`)}>
        <Pressable
          style={styles.button}
          onPress={() => onPress(`${mode}: button`)}
        />
      </Pressable>
    </View>
  );
}

function App() {
  // Counted as well as logged, so the same file proves the same thing on a
  // platform with no text engine: one pip per press that reached React.
  const [count, setCount] = React.useState(0);

  const onPress = React.useCallback(where => {
    console.log(`pressed ${where}`);
    setCount(previous => previous + 1);
  }, []);

  return (
    <View style={styles.page}>
      {MODES.map(mode => (
        <Row key={mode} mode={mode} onPress={onPress} />
      ))}
      <View style={styles.tally}>
        {Array.from({length: Math.min(count, 12)}, (_, index) => (
          <View key={index} style={styles.pip} />
        ))}
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltPointerEvents', () => App);
