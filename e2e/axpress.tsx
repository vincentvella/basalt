/**
 * A <Pressable> per accessibility role, each logging when it is activated.
 *
 * VoiceOver's activation (VO-Space, or any client's AXPress) has to reach
 * `onAccessibilityTap` whatever role the view announces. A radio button or a
 * switch that announces itself correctly but cannot be pressed is worse than
 * one that announces nothing, and a unit test calling the handler directly
 * cannot see it: AppKit decides which roles an outside press reaches.
 *
 * And a real <Switch>, which is a different case: React Native gives it no
 * tap handler and no checked state, so the host has to supply both. That was
 * the bug this app was written for -- the switch announced itself and could not
 * be pressed. The scenario is `test_accessibility_press`.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {AppRegistry, Pressable, StyleSheet, Switch, Text, View} from 'react-native';

const ROLES = ['button', 'radio', 'checkbox', 'switch', 'togglebutton', 'tab', 'link', 'menuitem'] as const;

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#f6f7f9', padding: 24, flexDirection: 'row', flexWrap: 'wrap'},
  chip: {width: 160, height: 48, margin: 6, borderRadius: 8, backgroundColor: '#4d8cf2', alignItems: 'center', justifyContent: 'center'},
  label: {color: '#ffffff', fontSize: 14},
  group: {flexDirection: 'row'},
});

// A real <Switch>, not a Pressable announcing one: it has its own native peer.
function Toggle(): React.ReactElement {
  const [on, setOn] = React.useState(false);
  return (
    <Switch
      accessibilityLabel="real switch"
      value={on}
      onValueChange={next => {
        setOn(next);
        console.log(`AXPRESS real switch ${next}`);
      }}
    />
  );
}

function App(): React.ReactElement {
  return (
    <View style={styles.page}>
      {ROLES.map(role => (
        <Pressable
          key={role}
          accessibilityRole={role}
          accessibilityLabel={`press ${role}`}
          accessibilityState={role === 'radio' || role === 'checkbox' || role === 'switch' ? {checked: false} : undefined}
          onAccessibilityTap={() => console.log(`AXPRESS ${role}`)}
          onPress={() => console.log(`PRESS ${role}`)}
          style={styles.chip}>
          <Text style={styles.label}>{role}</Text>
        </Pressable>
      ))}
      <Toggle />
      <View accessibilityRole="radiogroup" accessibilityLabel="group" style={styles.group}>
        {['one', 'two'].map(name => (
          <Pressable
            key={name}
            accessibilityRole="radio"
            accessibilityLabel={`grouped ${name}`}
            accessibilityState={{checked: name === 'one'}}
            onAccessibilityTap={() => console.log(`AXPRESS grouped ${name}`)}
            style={styles.chip}>
            <Text style={styles.label}>grouped {name}</Text>
          </Pressable>
        ))}
      </View>
    </View>
  );
}

AppRegistry.registerComponent('AxPress', () => App);
