/**
 * A React Native app whose whole job is to react to a cursor that is not
 * pressing anything.
 *
 * Hover is the one piece of input a desktop has and a phone does not, so it is
 * also the one part of the input path with no equivalent in React Native's
 * touch model. `onPointerEnter` and friends are W3C pointer events, which React
 * Native carries alongside touches; what this app exercises is a host actually
 * emitting them, the event beat delivering them, and React's own dispatch
 * telling enter from over.
 *
 * The nesting is the point. `over` and `out` bubble, so the card hears about a
 * cursor arriving anywhere inside it, including on each box; `enter` and
 * `leave` do not, so the card hears those exactly once no matter how many boxes
 * the cursor crosses. A host that confuses the two looks right here on the
 * first move and wrong on the second, which is what the logged lines assert.
 *
 * Every event is logged, which is what the end-to-end suite reads under
 * BASALT_TEST_HOVER.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  card: {
    height: 220,
    flexDirection: 'row',
    alignItems: 'center',
    padding: 20,
    borderRadius: 16,
    backgroundColor: '#1a1e28',
  },
  cardHovered: {backgroundColor: '#242a38'},
  box: {width: 140, height: 140, borderRadius: 12, marginRight: 20},
  tally: {flexDirection: 'row', marginTop: 24},
  pip: {
    width: 32,
    height: 32,
    borderRadius: 16,
    marginRight: 10,
    backgroundColor: '#56c98a',
  },
});

// A box that says, in the log, exactly which of the five events reached it.
function Box({name, color, hoveredColor, onEvent}) {
  const [hovered, setHovered] = React.useState(false);

  return (
    <View
      style={[styles.box, {backgroundColor: hovered ? hoveredColor : color}]}
      onPointerEnter={() => {
        setHovered(true);
        onEvent(`enter ${name}`);
      }}
      onPointerLeave={() => {
        setHovered(false);
        onEvent(`leave ${name}`);
      }}
      onPointerOver={() => onEvent(`over ${name}`)}
      onPointerOut={() => onEvent(`out ${name}`)}
    />
  );
}

function App() {
  // Counted rather than only logged, so the same file proves the same thing on
  // a platform with no text engine: one pip per event that arrived.
  const [count, setCount] = React.useState(0);
  const [cardHovered, setCardHovered] = React.useState(false);

  const onEvent = React.useCallback(name => {
    console.log(`hover: ${name}`);
    setCount(previous => previous + 1);
  }, []);

  return (
    <View style={styles.page}>
      <View
        style={[styles.card, cardHovered && styles.cardHovered]}
        onPointerEnter={() => {
          setCardHovered(true);
          onEvent('enter card');
        }}
        onPointerLeave={() => {
          setCardHovered(false);
          onEvent('leave card');
        }}>
        <Box
          name="left"
          color="#4285f4"
          hoveredColor="#8ab4f8"
          onEvent={onEvent}
        />
        <Box
          name="right"
          color="#f26f56"
          hoveredColor="#f7a08e"
          onEvent={onEvent}
        />
      </View>
      <View style={styles.tally}>
        {Array.from({length: Math.min(count, 12)}, (_, index) => (
          <View key={index} style={styles.pip} />
        ))}
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltHover', () => App);
