/**
 * A React Native app that is mostly accessibility props.
 *
 * Roles, labels, hints, states and hiding -- the things a screen reader is told
 * and a screenshot cannot show. Which is exactly why it is worth comparing
 * across platforms: an accessibility tree that is wrong looks identical to one
 * that is right until somebody turns VoiceOver or Orca on.
 *
 * `describeTree` reports React Native's own role name on both hosts, so this
 * file's output is comparable line for line. Whether that name was really
 * mapped onto NSAccessibility or GtkAccessibleRole is asserted in each
 * platform's own unit tests, which is where a platform question belongs.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Platform, Pressable, StyleSheet, Text, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#f6f7f9', padding: 24},
  row: {flexDirection: 'row', marginBottom: 16},
  chip: {
    width: 150,
    height: 56,
    marginRight: 12,
    borderRadius: 8,
    backgroundColor: '#4d8cf2',
    alignItems: 'center',
    justifyContent: 'center',
  },
  label: {color: '#ffffff', fontSize: 14},
  heading: {fontSize: 22, color: '#1f2129', marginBottom: 12},
  note: {fontSize: 14, color: '#6b7280'},
  hidden: {width: 150, height: 56, backgroundColor: '#e5e7eb', borderRadius: 8},
});

function App() {
  // One change, a beat after mount: long enough that the first mount is its own
  // transaction, short enough that a three-second run sees both.
  const [status, setStatus] = React.useState('Saving');
  React.useEffect(() => {
    const timer = setTimeout(() => setStatus('Saved'), 1000);
    return () => clearTimeout(timer);
  }, []);

  return (
    <View style={styles.page}>
      {/* A <Text> is text without being told so. */}
      <Text style={styles.heading}>Accessibility</Text>

      <View style={styles.row}>
        <Pressable
          style={styles.chip}
          accessibilityRole="button"
          accessibilityLabel="Save"
          accessibilityHint="Writes the file to disk"
          // The prop every app under test sets and no host read until
          // 2026-10-09: an identifier for whoever is driving the app from
          // outside. Each platform publishes it its own way -- UIA's automation
          // id, AppKit's accessibility identifier, GTK's accessible id -- and
          // the tree dump carries React Native's spelling so all three compare.
          testID="save-button">
          <Text style={styles.label}>Save</Text>
        </Pressable>

        <Pressable
          style={styles.chip}
          accessibilityRole="checkbox"
          accessibilityLabel="Wrap lines"
          accessibilityState={{checked: true}}
          // accessibilityViewIsModal: a screen reader should stay inside this
          // view rather than reading the ones behind it. Each platform has its
          // own name for it -- aria-modal, accessibilityModal, UIA's IsDialog
          // -- and the dump prints React Native's.
          accessibilityViewIsModal={true}>
          <Text style={styles.label}>Checked</Text>
        </Pressable>

        <Pressable
          style={styles.chip}
          accessibilityRole="button"
          accessibilityLabel="Delete"
          accessibilityState={{disabled: true}}>
          <Text style={styles.label}>Disabled</Text>
        </Pressable>
      </View>

      <View style={styles.row}>
        <View style={styles.chip} accessibilityRole="link" accessibilityLabel="Documentation" />
        <View style={styles.chip} accessibilityRole="adjustable" accessibilityLabel="Volume" />
        <View style={styles.chip} accessibilityRole="list" accessibilityLabel="Results" />
      </View>

      <View style={styles.row}>
        {/* Announced as nothing at all: the app asked for silence. */}
        <View style={styles.hidden} accessibilityRole="none" />
        {/* And this one is hidden along with everything inside it. */}
        <View style={styles.hidden} accessibilityElementsHidden={true}>
          <Text style={styles.note}>Not announced</Text>
        </View>
      </View>

      {/*
        accessibilityLabelledBy: the field's name lives on the caption after it,
        named by nativeID. Deliberately in this order -- the field first -- because
        that is the ordering that makes the relation hard: Fabric mounts in tree
        order, so the field is on screen before the view it names exists.
      */}
      <View style={styles.row}>
        <View
          style={styles.hidden}
          accessibilityRole="button"
          accessibilityLabelledBy="save-caption"
        />
        <Text style={styles.note} nativeID="save-caption">
          Save the document
        </Text>
      </View>

      {/*
        accessibilityLiveRegion: a status line that is read out when it changes,
        without a screen reader having to be on it. The text changes once, a
        second after mount, so a run can tell the first sighting -- which must
        announce nothing -- from the change, which must announce the new text.
      */}
      <Text
        style={styles.note}
        accessibilityLiveRegion="polite"
        nativeID="status">
        {status}
      </Text>

      <Text style={styles.note}>A plain View is scenery and stays out of the tree.</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltA11y', () => App);
