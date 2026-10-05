/**
 * Native screens for react-navigation, on a desktop.
 *
 * ## What this is for
 *
 * react-navigation's native stack renders every screen it has pushed and
 * leaves it to the platform to show the top one. Basalt implements the
 * components that do that, `RNSScreen` and `RNSScreenStack`, and on Windows
 * they are used: `Platform.OS` is `windows`, which react-native-screens has on
 * its own list of platforms it ships native code for.
 *
 * `macos` and `linux` are not on that list, and cannot be, because
 * react-native-macos reports `macos` too and the library has no macOS
 * implementation behind it. So on those two the library renders plain views
 * instead, every pushed screen stays mounted, and a screen reader can reach the
 * one behind the one you are on.
 *
 * ## Why this is not a patch
 *
 * The gate lives inside `InnerScreen`, and `ScreenContext` is the library's own
 * way of replacing that component:
 *
 *     const ScreenWrapper = React.useContext(ScreenContext) || InnerScreen;
 *
 * So a provider is enough. Nothing here forks the library, pins a version of
 * it, or rewrites anything in `node_modules`; it uses a public extension point
 * and goes out of the way the moment it is not wrapped around anything.
 *
 * `ScreenStack` needs nothing: it is not gated and renders its native
 * component everywhere already. Only the screens inside it were missing.
 *
 * ## When this stops being necessary
 *
 * software-mansion/react-native-screens#4779 proposes letting a platform that
 * supplies the components say so with `enableScreens()`. If that lands, this
 * package becomes one line in an application instead, and the provider can go.
 *
 * @format
 */

import * as React from 'react';

/**
 * `RNSScreen`, taken from the library rather than declared here.
 *
 * Declaring it needs a base view config, and reaching React Native's through
 * `require` pulled this package into a module cycle: with both this and
 * react-native-screens in one graph, whichever was mid-initialisation came
 * back `undefined`, and the app died on a property of undefined rather than on
 * anything that named the cause.
 *
 * So the component comes from the library that already registered it. It is a
 * path into its `src/`, which is a dependency on a layout that is not ours and
 * is the reason this package pins a minimum version rather than a range.
 */
// eslint-disable-next-line @typescript-eslint/no-var-requires
const RNSScreen = require('react-native-screens/src/fabric/ScreenNativeComponent')
  .default as React.ComponentType<Record<string, unknown>>;

type ScreenProps = {
  active?: number;
  activityState?: number;
  stackPresentation?: string;
  style?: unknown;
  children?: React.ReactNode;
  [key: string]: unknown;
};

/**
 * A screen, rendered natively, skipping the platform check in `InnerScreen`.
 *
 * `activityState` is derived from `active` the way the library derives it, for
 * the older callers that still send the boolean. Everything else is passed
 * through: the props a desktop does not have, the iOS sheet detents and the
 * rest, are filtered out by the view config above rather than here, so this
 * does not have to know which ones those are.
 */
const NativeScreen = React.forwardRef<unknown, ScreenProps>(
  function NativeScreen({active, activityState, children, ...rest}, ref) {
    const state = activityState ?? (active !== 0 ? 2 : 0);
    return (
      <RNSScreen ref={ref} activityState={state} {...rest}>
        {children}
      </RNSScreen>
    );
  },
);

/**
 * Wrap a navigator in this to get native screens on every desktop.
 *
 *     <NavigationScreens>
 *       <NavigationContainer>{...}</NavigationContainer>
 *     </NavigationScreens>
 *
 * Harmless on Windows, where the library already renders the same components,
 * and harmless on iOS and Android for the same reason. An application that
 * renders this on web would be asking for components the web build does not
 * have, which is the one place not to put it.
 */
export function NavigationScreens({children}: {children?: React.ReactNode}) {
  // Resolved at render rather than at import.
  //
  // At module scope this came back undefined, and the app died on
  // "Cannot read property 'ScreenContext' of undefined". Which import in the
  // cycle is unfinished depends on the order the application happens to import
  // things in, and that is not a thing a library should be sensitive to. The
  // context is not needed until something renders, so it is not asked for
  // until then.
  // eslint-disable-next-line @typescript-eslint/no-var-requires
  const {ScreenContext} = require('react-native-screens') as {
    ScreenContext: React.Context<React.ComponentType<never>>;
  };
  return (
    <ScreenContext.Provider value={NativeScreen as never}>
      {children}
    </ScreenContext.Provider>
  );
}

/** The screen component itself, for an application wiring its own provider. */
export {NativeScreen};
