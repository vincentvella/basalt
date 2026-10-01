# Design

## Context

See proposal.md - Why.

The constraint that makes this different from React Native's situation: there is
no dynamic native module loading here. A host is a single binary, built by
`react-native run-<platform> --build`, and anything native has to be compiled
into it. So "a separate package" cannot mean "a package the host knows nothing
about" -- it has to mean a package the *build* discovers.

That mechanism already exists and is proven three times over. `desktop.js`
finds `expo-modules-core`, `react-native-worklets` and `react-native-reanimated`
in the app's `node_modules` and passes each as `-DBASALT_<NAME>=<dir>`; the
host's CMake compiles their C++ when the define is present. What it is not is
general: the CLI knows those three by name.

## Goals / Non-Goals

- Goal: a rule for the boundary that can be applied without re-litigating it per
  capability.
- Goal: any package can contribute native code to the host build, discovered
  rather than hardcoded.
- Non-Goal: splitting the three host packages. `basalt-gtk`,
  `-appkit` and `-win32` are the hosts; that boundary is already right.
- Non-Goal: matching Electron's module list. Electron is a reference for what a
  desktop does, not for how a package is shaped.

## Decisions

**The rule: core is the application's own surface. A package is anything that
reaches outside it.**

Three tests, and failing *any one* puts a capability in its own package:

1. *Does the operating system ask the person for consent?* A permission prompt
   is the system saying this is not ordinary application behaviour. Camera,
   microphone, screen recording, location, full disk access -- and
   notifications, which is the one already shipped that this catches.
2. *Does it touch hardware, or another application?* A camera is hardware. Drag
   and drop carries data across an application boundary. A tray icon lives in
   another process's UI.
3. *Does it act outside the app's own windows?* A global shortcut fires when the
   app is not focused. Power and idle monitoring observe the machine. An
   auto-updater runs against a server.

Everything else is core, and that is most of what an app touches: its windows,
its menus, its dialogs, its title bar, its components, its input, and the React
Native APIs this platform implements.

**Menus, dialogs and the title bar stay in core.** An earlier draft of this
document moved them, on the reasoning that an app could ship without them. That
test was wrong: by it, almost everything is optional and the boundary lands
nowhere useful. A file dialog is the person choosing a file inside the app's own
flow, a menu is the window's furniture, and a title bar is part of the window. A
desktop platform that made you install a package for a menu would be a worse
platform, not a smaller one.

**Sorting what is catalogued by the rule:**

- **Core**: windows and their geometry, menus and context menus, file dialogs,
  the title bar, the component and input surface, clipboard, sharing, linking,
  appearance, developer tools, packaging, displays, window state persistence,
  progress in the dock or taskbar, a window representing a file.
- **Its own package**: notifications, camera, microphone, screen capture,
  location, drag and drop, the tray icon, global shortcuts, power and idle,
  secure storage, auto-update, crash reporting, file-system watching and
  bookmarks, in-app purchase, spell checking where it needs a downloaded
  dictionary.

**Notifications is the instructive one.** It is shipped, it lives in core as
`core/Notifications.h`, and it is permission-gated -- so the rule moves it. The
JavaScript API is already `expo-notifications`, a package, which means the seam
is the only part in the wrong place. That is what this rule looks like applied
to something real rather than something hypothetical.

**A contributing package declares itself in its own manifest**, rather than the
CLI knowing its name. A `basalt` key naming a CMake entry point is enough:

    "basalt": { "native": "native/CMakeLists.txt" }

The CLI scans the app's dependencies for that key and passes each directory in,
which is what `optionalNativeModules` does now with a hardcoded list of three.
Expo, worklets and Reanimated keep their special cases only because they are
third-party packages that will never carry a `basalt` key.

**One package per capability, not one per desktop.** A capability only one
desktop can do still ships as a single package, which installs and imports the
same way everywhere and does nothing on the desktops that cannot do it.

This is what the platform already does rather than a new idea. `<TitleBar>`
accepts every call on a host with no title bar module and reports zero metrics;
`windowControl`'s mutations are no-ops where GTK4 removed the call;
`Menu.isSupported` is false on GNOME. The same app runs on three desktops
unchanged, which is the property worth protecting -- a package per desktop would
mean an app importing different modules per platform, which is the thing this
platform exists to avoid.

A no-op SHALL be paired with a way to ask, for the same reason `capabilities`
exists on the window: an app should be able to hide a control rather than offer
one that silently does nothing. Doing nothing quietly is right for a call an app
makes anyway; it is wrong as the only answer available.

**Version together, at first.** Separate packages do not have to mean
independent versions, and independent versions of packages sharing a C++ ABI
with the host is a support problem nobody here wants yet.

## Risks / Trade-offs

- More packages is more install friction, which is the cost `npx
  basalt-core init` exists to absorb. If `init` does not add the
  packages an app needs, the split is worse than not splitting.
- A wrong boundary is harder to fix than no boundary, because moving twice is
  worse than moving once. This is why the rule is written down rather than
  applied case by case.
- Discovery by manifest key means a package can contribute C++ to a host binary
  by being installed. That is the same trust model as any native module on iOS
  or Android, and worth saying out loud.

## Open Questions

- Spell checking sits on the line. The platform's own dictionary is ordinary
  text behaviour and belongs in core; one that downloads a language is not.
  Which half this platform implements decides where it goes.
- Does moving the notifications seam out of core break the expo-notifications
  proxy, which several demos depend on? It should not -- the proxy is already a
  package -- but it is the first move and worth proving before the others.
