/**
 * The two react-native-screens host components, with their view configs.
 *
 * Written out rather than produced by `codegenNativeComponent`, which is what
 * the library itself uses. That call is rewritten into a static config at build
 * time by `@react-native/babel-plugin-codegen`, and the plugin wants a package
 * with a `codegenConfig` around it; `js/` is a directory of demo apps and has
 * neither. Left to run at runtime the call asks
 * `UIManager.getViewManagerConfig`, which the new architecture does not answer,
 * and the app dies with "View config not found for component `RNSScreen`".
 *
 * `NativeComponentRegistry.get` is what the generated code calls in the end, so
 * this is that code with the generator's step done by hand. The attribute lists
 * are the contract: a prop absent from `validAttributes` is dropped in
 * JavaScript and never reaches C++, which is how `activityState` would silently
 * stop working.
 *
 * @format
 */

'use strict';

const NativeComponentRegistry = require('react-native/Libraries/NativeComponent/NativeComponentRegistry');
// The base every view config builds on, and the file basalt itself overrides
// to add the three pointer props React Native forgot to declare. Taking it from
// here rather than listing attributes means a <Screen> accepts everything a
// <View> does, styles included.
const BaseViewConfig = require('react-native/Libraries/NativeComponent/BaseViewConfig').default;

const base = BaseViewConfig.validAttributes;

exports.Screen = NativeComponentRegistry.get('RNSScreen', () => ({
  uiViewClassName: 'RNSScreen',
  bubblingEventTypes: {},
  directEventTypes: {},
  validAttributes: {
    ...base,
    // The two the stack reads. See core/ScreensComponent.h.
    activityState: true,
    stackPresentation: true,
  },
}));

exports.ScreenStack = NativeComponentRegistry.get('RNSScreenStack', () => ({
  uiViewClassName: 'RNSScreenStack',
  bubblingEventTypes: {},
  directEventTypes: {},
  validAttributes: {...base},
}));
