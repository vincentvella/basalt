# basalt-core

React Native on the desktop: Linux, macOS and Windows, on React Native's own
C++ core rather than a fork of it or a browser engine.

```bash
npx basalt-core init      # in an app you already have
npm run macos -- --build  # or linux, or windows
```

[Documentation](https://basaltjs.dev) ·
[Getting started](https://basaltjs.dev/docs/getting-started) ·
[Platform support](https://basaltjs.dev/docs/support) ·
[GitHub](https://github.com/vincentvella/basalt)

Your components, props and layout behave the way they already do, because what
runs underneath is React Native's: Hermes, Yoga, Fabric and the shadow tree are
the same C++ that runs on a phone. Basalt supplies the part ReactCommon
declares and leaves for a platform to define, which is a mounting manager and a
text layout engine per desktop.

So a `<View>` is a real GTK4, AppKit or Win32 widget, and a paragraph is laid
out by Pango, Core Text or DirectWrite. The app ships as one binary. There is
no DOM, no CSS and no bundled browser.

## What is in this package

The half that is not about a toolkit:

- the **platform module**, so `Platform.OS` is `linux`, `macos` or `windows`
  and a `Button.macos.js` resolves;
- the **Metro configuration** that makes that happen, `withDesktopPlatforms`;
- **`basalt init`** and **`basalt doctor`**, which configure an app and report
  what a build needs;
- **`basalt-bundle`**, for apps whose CLI is Expo's rather than React Native's;
- React Native's C++ core and Hermes, built from source, and the parts of this
  platform that have nothing to do with a toolkit: the mounting walk, the core
  TurboModules, the Expo runtime, the image and font seams.

It builds and links with no toolkit at all, which is how that claim stays
honest rather than aspirational.

The desktop itself comes from a host package, which `init` adds for you:
[`basalt-gtk`](https://www.npmjs.com/package/basalt-gtk) for Linux,
[`basalt-appkit`](https://www.npmjs.com/package/basalt-appkit) for macOS,
[`basalt-win32`](https://www.npmjs.com/package/basalt-win32) for Windows.

## The APIs React Native has no equivalent for

Because a phone has none of these things:
[windows and second windows](https://basaltjs.dev/docs/reference/windows),
[menus and context menus](https://basaltjs.dev/docs/reference/menus),
[file dialogs](https://basaltjs.dev/docs/reference/dialogs),
[drag and drop](https://basaltjs.dev/docs/reference/drag-and-drop),
[keyboard shortcuts](https://basaltjs.dev/docs/reference/keyboard),
[the title bar](https://basaltjs.dev/docs/reference/title-bar) and the list of
displays.

```jsx
import {useWindow, Menu, TitleBar} from 'basalt-core';
```

## What to expect

- **React Native 0.86, 0.87 and 0.88.** CI builds and tests against 0.88.1,
  which is what Expo SDK 58 installs. On 0.86 there is no Fast Refresh, for a
  reason that was root-caused and fixed upstream in 0.87.
- **The first build compiles Hermes and React Native's C++ from source** and
  takes tens of minutes. Later builds do not. `npx basalt-core doctor` checks
  your toolchain in seconds before you spend that.
- **This is `0.2.x`.** Semver promises nothing below 1.0 and that is meant
  literally: the API can change in any release.
- **The three desktops are not equally finished.** Linux and macOS are ahead of
  Windows, and [the support
  table](https://basaltjs.dev/docs/support) says exactly where, prop by prop.

## Licence

MIT.
