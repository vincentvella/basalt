# basalt-navigation

Native screens for react-navigation on the desktop.

```bash
npm install basalt-navigation
```

```jsx
import {NavigationScreens} from 'basalt-navigation';

<NavigationScreens>
  <NavigationContainer>{/* navigators, as usual */}</NavigationContainer>
</NavigationScreens>
```

That is the whole API. Wrap it once, as high as you like.

## What it does

react-navigation's native stack renders every screen it has pushed and leaves
it to the platform to show the top one. Basalt implements the components that
do that, and on Windows they are used already. On macOS and Linux
react-native-screens renders plain views instead, so every pushed screen stays
mounted: invisible, because the screen in front covers it, but still measured
and still reachable by a screen reader.

This makes those two behave like Windows.

## What it is not

Not a fork, not a patch, and nothing is rewritten in `node_modules`.
react-native-screens picks the component it renders for each screen out of a
React context:

```js
const ScreenWrapper = React.useContext(ScreenContext) || InnerScreen;
```

The platform check lives inside `InnerScreen`, so providing a different screen
through that context is enough, and it is a public extension point rather than
a crack. `ScreenStack` needs nothing at all: it is not gated and renders its
native component everywhere already.

## When you will not need it

[react-native-screens#4779](https://github.com/software-mansion/react-native-screens/pull/4779)
proposes letting a platform that supplies the components say so with
`enableScreens()`. If it lands, this package becomes one line in your
application and can go.

## The one wart

The screen component is taken from `react-native-screens/src/fabric/`, which
is a path inside the library rather than part of its public API, so a release
that moves it would break this. Declaring the component here instead was tried
and is worse: it needs React Native's base view config, and requiring that
pulled this package into a module cycle where react-native-screens came back
`undefined` and the application died on a property of undefined. A path that
can break loudly on upgrade beat a cycle that breaks confusingly at runtime.
