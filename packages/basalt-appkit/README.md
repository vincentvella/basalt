# basalt-appkit

The macOS half of [Basalt](https://basaltjs.dev): an AppKit view layer, a
Fabric mounting manager, and a host that runs your React Native app as a
native application.

```bash
npx basalt-core init      # adds this package when app.json asks for macos
npm run macos -- --build
```

You rarely install this by hand. `basalt init` reads
[`basalt.desktops`](https://basaltjs.dev/docs/reference/app-json#basaltdesktops)
and adds the host packages the app asked for.

## What it gives you

An `NSView` per view, Core Text for every paragraph, and the menu bar the
system expects an application to own. VoiceOver is answered through
NSAccessibility, notifications go through `UNUserNotificationCenter`, and an
app that draws its own title bar gets the traffic lights positioned for it.

It registers two React Native CLI commands:

| | |
| --- | --- |
| `react-native run-macos` | builds nothing by default and runs your app against a packager |
| [`react-native package-macos`](https://basaltjs.dev/docs/reference/cli#react-native-package-macos) | a self-contained, signed `.app` that runs on a Mac other than the one that built it, and does not launch it |

A development run already produces an `.app`, because `NSBundle.mainBundle`
comes from where the executable sits rather than from how it was launched, and
notifications, the Dock icon and the menu bar name all need a bundle.

[What works, prop by prop](https://basaltjs.dev/docs/support).

## What you need to build it

```bash
xcode-select --install
brew install cmake ninja
```

The macOS SDK comes from the Command Line Tools. `npx basalt-core doctor`
checks for all of it in seconds, which is worth doing before a first build
that compiles Hermes and React Native's C++ from source and takes tens of
minutes.

Apple silicon and Intel.

## What it is not

Not a renderer and not a runtime. Everything behind the views is in
[`basalt-core`](https://www.npmjs.com/package/basalt-core): React Native's own
Hermes, Yoga, Fabric and shadow tree, the same C++ that runs on a phone. This
package is the part ReactCommon declares and leaves for a platform to define.

The host binary is `basalt_appkit`, which is what shows in `ps` and in a crash
report rather than your app's name. A packaged `.app` carries the app's own
name in its window and its Dock tile.

## Licence

MIT.
