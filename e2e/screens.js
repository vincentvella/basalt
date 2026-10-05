/**
 * react-native-screens' stack, driven without react-native-screens.
 *
 * The components come from ScreensNativeComponent.js, declared the way the
 * library declares its own, so this exercises exactly the descriptors
 * core/ScreensComponent.h registers. Not the library itself, for two reasons:
 * `e2e/` has no node_modules, and react-native-screens refuses its native path
 * on a desktop anyway, because `isNativePlatformSupported` lists ios, android
 * and windows and is a const. Testing through it would test that gate.
 *
 * Three screens, arranged so one run checks every rule at once.
 *
 * The top one is a transparent modal, so it covers nothing and the screen under
 * it stays visible; the middle one is opaque, so everything under *that* is
 * hidden. Expected: bottom hidden, middle and top not.
 *
 * The middle screen also carries a header with a subview in it, and the bottom
 * one a header with nothing, which are the two cases that behave differently: a
 * header with content becomes a bar and pushes the content down, and an empty
 * one takes itself out of the layout rather than leaving a blank strip.
 *
 * @format
 */

'use strict';

const React = require('react');
const {AppRegistry, Text, View} = require('react-native');
const {
  Screen,
  ScreenStack,
  HeaderConfig,
  HeaderSubview,
} = require('./ScreensNativeComponent');

const fill = {position: 'absolute', top: 0, left: 0, right: 0, bottom: 0};

function Screens() {
  return (
    <View style={{flex: 1}}>
      <ScreenStack style={{flex: 1}}>
        <Screen activityState={2} style={fill}>
          <Text>bottom screen</Text>
          {/* Nothing in it, so it should disappear rather than reserve a bar. */}
          <HeaderConfig />
        </Screen>
        <Screen activityState={2} style={fill}>
          <View style={fill}>
            <Text>middle screen</Text>
          </View>
          <HeaderConfig>
            <HeaderSubview>
              <Text>middle title</Text>
            </HeaderSubview>
          </HeaderConfig>
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
