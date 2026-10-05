/**
 * A React Native app that reacts to dark mode.
 *
 * `useColorScheme()` is the hook every real app uses, and it is
 * `Appearance.getColorScheme()` plus a change listener. Both halves are
 * exercised here: the initial read decides the palette, and the listener is
 * what makes the second half of this file worth having.
 *
 * The scheme is then driven from JavaScript with `Appearance.setColorScheme`,
 * which is the part most likely to be subtly wrong: an override has to win over
 * the system for every later read, and has to notify listeners itself, since
 * nothing in the operating system changed.
 *
 * It also makes this app comparable across the two hosts. The *initial* scheme
 * is whatever each machine is set to, so an app that only read it would render
 * differently on a dark Mac and a light Linux box -- correctly, and uselessly
 * for a diff. Forcing one makes both converge, and the initial reading is
 * logged instead of rendered.
 *
 * The palette is rendered as boxes rather than text, so the tree says which
 * scheme was in force without depending on a text engine.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {Appearance, AppRegistry, Platform, StyleSheet, useColorScheme, View} from 'react-native';

console.log(`Platform.OS is ${Platform.OS}`);
console.log(`initial colour scheme: ${Appearance.getColorScheme()}`);

const PALETTES = {
  light: {page: '#f6f7f9', card: '#ffffff', accent: '#4d8cf2'},
  dark: {page: '#11131a', card: '#1a1e28', accent: '#9b59f6'},
};

const styles = StyleSheet.create({
  page: {flex: 1, padding: 24},
  card: {height: 120, borderRadius: 12, padding: 16, marginBottom: 16},
  accent: {width: 120, height: 40, borderRadius: 8},
});

function App() {
  const scheme = useColorScheme() ?? 'light';
  const palette = PALETTES[scheme] ?? PALETTES.light;

  React.useEffect(() => {
    console.log(`rendering for ${scheme}`);
  }, [scheme]);

  React.useEffect(() => {
    // Through both states, ending on dark. Two reasons for the round trip.
    //
    // Whichever way this machine is set, one of the two is a real change, so
    // the re-render is observable on a dark Mac and on a light Linux box alike
    // -- and nothing outside the process moved, so the notification has to come
    // from the override itself or `useColorScheme` never hears about it.
    //
    // And it ends somewhere deterministic. The *initial* scheme is whatever
    // each machine is set to, so an app that only read it would render
    // differently on the two hosts -- correctly, and uselessly for a diff.
    const toLight = setTimeout(() => {
      console.log('override: light');
      Appearance.setColorScheme('light');
    }, 500);
    const toDark = setTimeout(() => {
      console.log('override: dark');
      Appearance.setColorScheme('dark');
    }, 1000);
    const release = setTimeout(() => {
      // 'auto', not null: ColorSchemeOverride is 'light' | 'dark' | 'auto' |
      // 'unspecified', and passing null throws in the bridging layer before it
      // reaches any platform.
      console.log(`system says: ${(Appearance.setColorScheme('auto'), Appearance.getColorScheme())}`);
      // Back to dark, so both hosts finish in the same place whatever their
      // machine is set to.
      Appearance.setColorScheme('dark');
    }, 1500);
    return () => {
      clearTimeout(toLight);
      clearTimeout(toDark);
      clearTimeout(release);
    };
  }, []);

  return (
    <View style={[styles.page, {backgroundColor: palette.page}]}>
      <View style={[styles.card, {backgroundColor: palette.card}]}>
        <View style={[styles.accent, {backgroundColor: palette.accent}]} />
      </View>
    </View>
  );
}

AppRegistry.registerComponent('BasaltAppearance', () => App);
