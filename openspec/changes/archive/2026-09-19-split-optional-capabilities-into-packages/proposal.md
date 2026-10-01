# Decide the package boundary before the next fifteen capabilities land in core

## Why

React Native shipped one package with everything in it -- AsyncStorage, WebView,
NetInfo, Clipboard, CameraRoll, Slider, ViewPager, push notifications -- and has
spent the decade since extracting them. The extraction is the expensive part:
every move is a breaking change, a migration guide, and a community package that
has to be adopted before core can drop the original.

This platform is at the point where that decision is cheap and about to stop
being. Thirty-three desktop capabilities are catalogued as open. Today
`basalt-core` holds every one that has shipped -- windows, menus,
dialogs, context menus, the title bar -- and nothing decides where the next one
goes. A camera, a tray icon, power monitoring, global shortcuts and drag and drop are
not the same kind of thing as `<View>` or a menu, and putting them in the same
package is the decision that was made by not making it.

## What Changes

- A stated rule for what belongs in `basalt-core` and what belongs in a
  package of its own: core is the application's own surface -- its windows,
  menus, dialogs, components and input -- and a package is anything that reaches
  outside it, by needing the person's consent, by touching hardware or another
  application, or by acting when the app is not focused.
- Generalise the mechanism that already compiles an optional native module into
  the host. The CLI detects `expo-modules-core`, `react-native-worklets` and
  `react-native-reanimated` by name and passes each as a `-D` to CMake; that
  works and is a hardcoded list of three. A split needs any installed package to
  be able to contribute C++ to the host build without the CLI knowing its name.
- Apply the rule to what is already shipped: move what does not meet it, once,
  now, while there are no dependants.

## Capabilities

### Modified Capabilities
- `distribution`

## Impact

- `packages/basalt-core/cli/desktop.js` -- `optionalNativeModules`
  becomes discovery-driven rather than a list.
- A manifest convention for a package that contributes native code, and a CMake
  entry point it provides.
- Possibly moving shipped code out of `basalt-core`, which is a breaking
  change and is the reason to do it before anyone depends on it.
- `docs/backlog/ecosystem.md`, which says publishing is next: this decides what
  is being published.
