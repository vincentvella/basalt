---
sidebar_position: 1
title: What Basalt is
---

# What Basalt is

Basalt runs React Native apps on the desktop — Linux, macOS and Windows —
on React Native's own C++ core rather than on a browser engine.

Each desktop gets a host written against its native toolkit: GTK4 on Linux,
AppKit on macOS, Win32 with Direct2D and DirectWrite on Windows. A `<View>`
is a real widget on each, text is laid out by Pango, Core Text or DirectWrite,
and the app ships as one binary with no bundled browser.

:::info This page is a stub
The user-facing documentation has not been written yet. What exists today is
the repository's own documentation, under **Contributing** — written for people
working on Basalt rather than with it, but accurate and detailed.
:::

## Where things are

- **[Architecture](/contributing/ARCHITECTURE)** — how the hosts, the shared
  core and the mounting layer fit together.
- **[Decisions](/contributing/DECISIONS)** — why it is built this way, kept as
  the reasons were made rather than written up afterwards.
- **[Testing](/contributing/TESTING)** — what is checked, on which platform,
  and why some things can only be checked on one.
- **[Porting](/contributing/PORTING)** — what it takes to make a third-party
  native module work here.
