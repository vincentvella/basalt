# basalt-win32

The Windows half of [Basalt](https://basaltjs.dev): a Win32 and Direct2D view
layer, a Fabric mounting manager, and a host that runs your React Native app
as a native application.

```bash
npx basalt-core init      # adds this package when app.json asks for windows
npm run windows -- --build
```

You rarely install this by hand. `basalt init` reads
[`basalt.desktops`](https://basaltjs.dev/docs/reference/app-json#basaltdesktops)
and adds the host packages the app asked for.

## What it gives you

Win32 windows, Direct2D for everything drawn, and DirectWrite for every line
of text. Screen readers are answered through UI Automation. The caption
buttons stay the system's, and an app that draws its own header answers
`HTCAPTION` for the parts of it that should move the window.

A toast needs an AppUserModelID and a Start Menu shortcut carrying it, and the
host registers both itself at startup rather than from Node, because a `.lnk`
with that property set means `IPropertyStore`, which means COM.

It registers the `react-native run-windows` command.

**Windows is the least finished of the three desktops**, and
[the support table](https://basaltjs.dev/docs/support) says exactly where
rather than leaving you to find out: 124 of 137 props at the time of writing,
against 126 on Linux and 130 on macOS, with a reason beside every gap.

## What you need to build it

CMake, Ninja, [vcpkg](https://vcpkg.io) with `VCPKG_INSTALLATION_ROOT`
pointing at it, and **`clang-cl`**, not just `cl`: Hermes uses
`__builtin_expect`, which MSVC's compiler does not have.

`npx basalt-core doctor` checks all of that in seconds. Worth doing before a
first build that compiles Hermes and React Native's C++ from source and takes
tens of minutes.

Ninja is what development and CI use. The Visual Studio generator works too,
and CI builds it on every release so that opening the project in Visual Studio
keeps working.

## What it is not

Not a renderer and not a runtime. Everything behind the views is in
[`basalt-core`](https://www.npmjs.com/package/basalt-core): React Native's own
Hermes, Yoga, Fabric and shadow tree, the same C++ that runs on a phone. This
package is the part ReactCommon declares and leaves for a platform to define.

The host binary is `basalt_win32`, which is what shows in Task Manager and in
a crash report rather than your app's name.

## Licence

MIT.
