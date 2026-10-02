---
sidebar_position: 2
title: Getting started
---

# Getting started

:::warning Stub
Not written yet. The commands below are real and current, but this page owes
you the context around them.
:::

```bash
npx basalt init          # configure an app to build for the desktop
npx basalt doctor        # report what a build needs, changing nothing
```

Then, through React Native's CLI:

```bash
npx react-native run-linux
npx react-native run-macos
npx react-native run-windows
```

## Packages

| package | what it is |
| --- | --- |
| `basalt-core` | the platform, the CLI and the shared C++ |
| `basalt-gtk` | the Linux host |
| `basalt-appkit` | the macOS host |
| `basalt-win32` | the Windows host |
| `basalt-subprocess` | running a command and watching its output |
| `basalt-notifications` | desktop notifications |

`basalt init` adds the ones an app needs. Which desktops it targets comes from
`app.json`, under `"basalt": {"desktops": [...]}`.
