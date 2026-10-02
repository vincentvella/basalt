/**
 * react-native-screens' stack, driven without react-native-screens.
 *
 * The components come from ScreensNativeComponent.js, declared the way the
 * library declares its own, so this exercises exactly the descriptors
 * core/ScreensComponent.h registers. Not the library itself, for two reasons:
 * `js/` has no node_modules, and react-native-screens refuses its native path
 * on a desktop anyway, because `isNativePlatformSupported` lists ios, android
 * and windows and is a const. Testing through it would test that gate.
 *
 * Three screens, arranged so one run checks both rules. The top one is a
 * transparent modal, so it covers nothing and the screen under it stays
 * visible; the middle one is opaque, so everything under *that* is hidden.
 * Expected: bottom hidden, middle and top not.
 *
 * @format
 */

'use strict';

const React = require('react');
const {AppRegistry, Text, View} = require('react-native');
const {Screen, ScreenStack} = require('./ScreensNativeComponent');

const fill = {position: 'absolute', top: 0, left: 0, right: 0, bottom: 0};

function Screens() {
  return (
    <View style={{flex: 1}}>
      <ScreenStack style={{flex: 1}}>
        <Screen activityState={2} style={fill}>
          <Text>bottom screen</Text>
        </Screen>
        <Screen activityState={2} style={fill}>
          <Text>middle screen</Text>
        </Screen>
        <Screen
          activityState={2}
          stackPresentation="transparentModal"
          style={fill}>
          <Text>top screen</Text>
        </Screen>
      </ScreenStack>
    </View>
  );
}

AppRegistry.registerComponent('BasaltScreens', () => Screens);
