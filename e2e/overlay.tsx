/**
 * A React Native app about React DevTools' element highlighter.
 *
 * `DebuggingOverlay` is the view DevTools mounts over an app to point at
 * something: the blue box over an inspected element, and the outlines around
 * everything that just re-rendered when "Highlight updates" is on. It is driven
 * entirely by commands rather than by props, which is why this app issues them
 * directly -- DevTools is the only other thing that would, and it needs a
 * DevTools session to be attached.
 *
 * Both kinds are here because they are drawn differently on purpose: an
 * inspected element is filled, a trace update is only outlined, and telling
 * them apart at a glance is the whole point. They are two screens rather than
 * one because one of them disappears on a timer; see below.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, StyleSheet, Text, View} from 'react-native';
import DebuggingOverlayNativeComponent, {
  Commands,
} from 'react-native/src/private/components/debuggingoverlay/specs/DebuggingOverlayNativeComponent';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  label: {color: '#aab', fontSize: 13, height: 22},
  card: {
    width: 200,
    height: 120,
    borderRadius: 10,
    backgroundColor: '#2b3140',
    marginBottom: 12,
  },
  overlay: {...StyleSheet.absoluteFill},
});

// Two apps rather than one, and the reason is timing rather than tidiness.
//
// An inspected element stays until it is cleared; a trace update takes itself
// down after a moment. Issuing both from one screen means the answer depends on
// when you look -- and a test that dumps the tree at the moment the trace
// update expires reads "nothing is drawn" as a failure. It did, on a CI machine
// and not on a developer's.
//
// So each kind gets a screen, and each screen has one stable answer: this one
// always shows a highlight, and the one below never does by the time anybody
// looks.
function Inspected() {
  const overlay = React.useRef(null);

  React.useEffect(() => {
    if (overlay.current == null) {
      return;
    }
    // Filled, in DevTools' blue, and it stays: nothing takes an inspected
    // element down but `clearElementsHighlights`.
    Commands.highlightElements(overlay.current, [
      {x: 24, y: 46, width: 200, height: 120},
    ]);
    console.log('overlay: highlighted an element');
  }, []);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>DebuggingOverlay: an inspected element</Text>
      <View style={styles.card} />
      <View style={styles.card} />
      <DebuggingOverlayNativeComponent ref={overlay} style={styles.overlay} />
    </View>
  );
}

function TraceUpdate() {
  const overlay = React.useRef(null);

  React.useEffect(() => {
    if (overlay.current == null) {
      return;
    }
    // Outlined, in the colour DevTools chose for how often this component has
    // re-rendered, and it clears itself after a moment because the point is
    // that it flashes.
    Commands.highlightTraceUpdates(overlay.current, [
      {
        id: 1,
        rectangle: {x: 24, y: 46, width: 200, height: 120},
        color: 0xff00c853,
      },
    ]);
    console.log('overlay: highlighted a trace update');
  }, []);

  return (
    <View style={styles.page}>
      <Text style={styles.label}>DebuggingOverlay: a trace update</Text>
      <View style={styles.card} />
      <DebuggingOverlayNativeComponent ref={overlay} style={styles.overlay} />
    </View>
  );
}

AppRegistry.registerComponent('BasaltOverlay', () => Inspected);
AppRegistry.registerComponent('BasaltOverlayTrace', () => TraceUpdate);
