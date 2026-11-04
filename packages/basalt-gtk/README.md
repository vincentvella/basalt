# basalt-gtk

The Linux half of [Basalt](https://basaltjs.dev): a GTK4 view layer, a Fabric
mounting manager, and a host that runs your React Native app as a native
application.

```bash
npx basalt-core init      # adds this package when app.json asks for linux
npm run linux -- --build
```

You rarely install this by hand. `basalt init` reads
[`basalt.desktops`](https://basaltjs.dev/docs/reference/app-json#basaltdesktops)
and adds the host packages the app asked for.

## What it gives you

A `<View>` is a real `GtkWidget`, and a paragraph is laid out by Pango over
the font the desktop chose, so narrowing the window re-wraps text the way it
does on a phone. Notifications go over D-Bus, the file dialogs are the portal
ones, so they work inside a Flatpak sandbox, and the accessibility tree is
AT-SPI, which means Orca reads an app that never mentioned Orca.

It also registers the `react-native run-linux` command, and writes a
`.desktop` file beside the build on every run, naming the app, its icon, its
`StartupWMClass` and any URL schemes it registered. That file is what gets a
notification the app's own name and icon rather than the binary's.

[What works, prop by prop](https://basaltjs.dev/docs/support).

## What you need to build it

```bash
sudo apt install cmake ninja-build libgtk-4-dev
```

A C++20 compiler and GTK 4's development headers. The host needs GTK **4.10**
or later, which is why Ubuntu 24.04 rather than 22.04, and CI builds against
the 4.14 that 24.04 ships. `pkg-config --exists gtk4` is the check, and
`npx basalt-core doctor` runs it
along with everything else in seconds. Worth doing first: a first build
compiles Hermes and React Native's C++ from source and takes tens of minutes,
and without it a missing prerequisite surfaces as a CMake error twenty minutes
in.

## What it is not

Not a renderer and not a runtime. Everything behind the widgets is in
[`basalt-core`](https://www.npmjs.com/package/basalt-core): React Native's own
Hermes, Yoga, Fabric and shadow tree, which are the same C++ that runs on a
phone. This package is the part ReactCommon declares and leaves for a platform
to define.

The host binary is `basalt_gtk`, which is what shows in `ps` and in a crash
report rather than your app's name. That is deliberate.

## Licence

MIT.
