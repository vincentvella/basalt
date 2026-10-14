<picture>
  <source media="(prefers-color-scheme: dark)" srcset="website/static/img/logo-inverse.svg">
  <img src="website/static/img/logo.svg" alt="" width="104" align="right">
</picture>

# Basalt

**React Native on the desktop: Linux, macOS and Windows, on React Native's own
C++ core rather than a fork of it or a browser engine.**

[Docs](https://basaltjs.dev) · [Platform support](https://basaltjs.dev/docs/support) · [Getting started](https://basaltjs.dev/docs/getting-started)

```bash
npx basalt-core init      # in an app you already have
npm run macos -- --build  # or linux, or windows
```

Your components, props and layout behave the way they already do, because what
runs underneath is React Native's: Hermes, Yoga, Fabric and the shadow tree are
the same C++ that runs on a phone. Basalt supplies the part ReactCommon
declares and leaves for a platform to define, which is a mounting manager and a
text layout engine per desktop.

So a `<View>` is a real GTK4, AppKit or Win32 widget, and a paragraph is laid
out by Pango, Core Text or DirectWrite, which is why narrowing the window
re-wraps it the way it does on a phone. The app ships as one binary. There is
no DOM, no CSS and no bundled browser.

## How it differs

**Electron and Tauri** give you a web view: you write web UI and a browser lays
it out. Basalt gives you the React Native component model over native widgets.

**react-native-windows and react-native-macos** are separate forks with
separate cadences, on React Native 0.84 and 0.83 today. Basalt is one project
covering three desktops, tracks 0.87, and deliberately uses their platform
names, so a library shipping `Button.macos.js` resolves here without knowing
Basalt exists.

**Expo** is not an alternative, it is the common case. A stock Expo app runs,
and `init` writes the Metro config on `expo/metro-config` when it finds one.

## Where the three desktops stand

| | Toolkit | Props implemented |
| --- | --- | --- |
| Linux | GTK4 | 126 of 136 |
| macOS | AppKit | 130 of 140 |
| Windows | Win32, Direct2D, DirectWrite | 124 of 136 |

Counted by a script rather than claimed: every field React Native's C++ parses,
with a row per attribute, a column per desktop and a reason wherever a host
does not read one. [The table is the honest version of this
section](https://basaltjs.dev/docs/support).

What is behind those numbers:

- `View`, `Text`, `Image`, `ScrollView`, `TextInput`, `Switch`,
  `ActivityIndicator`, `Modal` and `RefreshControl` mount to real widgets on
  all three, and answer a press, a wheel, a hover, a Tab and a keystroke with
  the events React Native says they should.
- `Alert`, `Share`, `Linking`, `Clipboard`, `Appearance`, `Animated` with the
  native driver, `require()`d assets, LogBox and the developer menu work
  everywhere.
- Reanimated, gesture-handler, React Native Skia and expo-image run, and
  react-navigation gets native screens: [what works with
  what](https://basaltjs.dev/docs/libraries). Your own C++ runs too, without
  publishing a package.
- Accessibility reaches each platform's own layer: AT-SPI, NSAccessibility and
  UI Automation.
- The desktop's own things, which React Native has no API for because a phone
  has none of them: [windows and second
  windows](https://basaltjs.dev/docs/reference/windows), [menus and context
  menus](https://basaltjs.dev/docs/reference/menus), [file
  dialogs](https://basaltjs.dev/docs/reference/dialogs), [drag and
  drop](https://basaltjs.dev/docs/reference/drag-and-drop), [keyboard
  shortcuts](https://basaltjs.dev/docs/reference/keyboard) and [the title
  bar](https://basaltjs.dev/docs/reference/title-bar).
- The three hosts agree on the view tree for every app in `e2e/`, which
  `scripts/compare_all.sh` checks on every CI run. Text geometry is the
  exception it allows: three text engines over three fonts do not put a
  paragraph on the same pixel, so for those apps the shape, the strings and the
  colours are compared and the frames are not.

## What to expect

Basalt is at `0.2.x` and published on npm. Semver promises nothing below 1.0
and that is meant literally: the API can change in any release. Being specific
about the limits is more useful than a maturity label.

- **React Native 0.86 and 0.87 are supported**, and 0.87 is what CI pins. On
  0.86, which Expo SDK 57 installs, there is no Fast Refresh: a teardown leaves
  the host without a mounting manager. That was root-caused and fixed upstream
  in 0.87.
- **The first build compiles Hermes and React Native's C++ from source** and
  takes tens of minutes. Later builds do not. `npx basalt-core doctor` checks
  your toolchain in seconds before you spend that.
- **The three desktops are not equally finished.** Linux and macOS are ahead of
  Windows, and the support table says exactly where.
- **No third-party native module has been ported end to end.** The TurboModule
  seam is proven and the libraries above run, but nothing has gone through the
  codegen path yet, so what porting an arbitrary module costs is still a guess.
  It is the largest unknown here.

## Documentation

| | |
| --- | --- |
| [Getting started](https://basaltjs.dev/docs/getting-started) | prerequisites, one command, and what it changes |
| [Platform support](https://basaltjs.dev/docs/support) | what works on which desktop, prop by prop |
| [CLI reference](https://basaltjs.dev/docs/reference/cli) | every command, option and default |
| [APIs](https://basaltjs.dev/docs/reference/api) | the things Basalt adds that React Native has no equivalent for, a page per area |
| [Libraries](https://basaltjs.dev/docs/libraries) | react-navigation, Skia, Reanimated, gesture-handler, and where Expo stands |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | how it is built, and the seam in React Native's C++ it hangs off |
| [`docs/BACKLOG.md`](docs/BACKLOG.md) | what is open, one file per area |

## Contributing

[`CONTRIBUTING.md`](CONTRIBUTING.md) is how to build this repository, what the
packages are, and how to run the tests. `docs/TESTING.md` is what is tested and
what is not.

This was called `react-native-linux` until it stopped being about Linux.

## Licence

MIT. See [`LICENSE`](LICENSE).
