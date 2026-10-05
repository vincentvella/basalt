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
  return (
    <View style={styles.page}>
      {/* A <Text> is text without being told so. */}
      <Text style={styles.heading}>Accessibility</Text>

      <View style={styles.row}>
        <Pressable
          style={styles.chip}
          accessibilityRole="button"
          accessibilityLabel="Save"
          accessibilityHint="Writes the file to disk">
          <Text style={styles.label}>Save</Text>
        </Pressable>

        <Pressable
          style={styles.chip}
          accessibilityRole="checkbox"
          accessibilityLabel="Wrap lines"
          accessibilityState={{checked: true}}>
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

      <Text style={styles.note}>A plain View is scenery and stays out of the tree.</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltA11y', () => App);
