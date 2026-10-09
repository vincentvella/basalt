# Backlog

Not scheduled. One file per area, because this was a single 1,200-line document
and finding anything in it meant reading all of it.

Counts are open entries. Nothing is lost; the prose moved, it did not shrink.

## What a desktop still owes

| Area | Open | What is left there |
| --- | --- | --- |
| [Desktop capabilities](backlog/desktop-capabilities.md) | 31 | Windows, menus, dialogs and dragging are done; the catalogue at the end of that file is the rest of the surface (tray, permissions, power, global shortcuts) checked one at a time against the repository. |
| [Ecosystem](backlog/ecosystem.md) | 3 | Nobody else can use this yet: nothing is published. Porting a first third-party native module, and packaging for Arch and Flatpak. |
| [Core modules](backlog/core-modules.md) | 7 | React Native APIs with no implementation here. |
| [Expo](backlog/expo.md) | 11 | Beyond the template: more Expo views, Reanimated's untried paths, and the gestures a cursor cannot make. |

## Components and behaviour

| Area | Open | What is left there |
| --- | --- | --- |
| [Input](backlog/input.md) | 4 | Multi-touch is not modelled, Wayland input is unverified, `setIsJSResponder` is a no-op. |
| [Text](backlog/text.md) | 4 | Measurement and layout gaps. |
| [TextInput](backlog/textinput.md) | 6 | Keyboard types, spell check, the placeholder and selection colours. |
| [Image](backlog/image.md) | 4 | Decorative props, and assets over the network: fetching, caching, progress. |
| [ScrollView](backlog/scrollview.md) | 4 | Trackpad scrolling unverified, zoom, `contentBoundingRect`, view culling. |
| [Components](backlog/components.md) | 5 | What is not implemented at all, and `<Modal>` as a real window. |
| [Accessibility](backlog/accessibility.md) | 5 | Nothing has been tested against a real screen reader. |
| [Correctness](backlog/correctness.md) | 2 | Things that work but not quite right. |

## Platforms and plumbing

| Area | Open | What is left there |
| --- | --- | --- |
| [Windows, the platform](backlog/platform-windows.md) | 16 | A peer since phase 47. What is left is named, and two entries are missing halves: an inline <View> inside a <Text>, and six style props. |
| [macOS](backlog/platform-macos.md) | 7 | Justified text, fonts, the rest of accessibility. |
| [Host wiring](backlog/host-wiring.md) | 7 | Dev support, error reporting, the offline `__DEV__` bundle. |
| [Compatibility](backlog/compatibility.md) | 7 | Which React Native versions work, and which cannot. |
| [Testing](backlog/testing.md) | 9 | What the suites cannot see. Was two sections with the same name, 800 lines apart; merged. |
| [Upstream](backlog/upstream.md) | 19 | Bugs and gaps in React Native and Expo, with the workarounds here. |

## How to use this

Add an entry to the file for its area, not here; the counts above are a snapshot
and will drift, which is fine. Strike an entry through when it is done rather
than deleting it: the order things were done in is the useful part, and
[Windows](backlog/platform-windows.md) is mostly struck-through for that reason.

An entry earns its length by saying *why*, not by saying more. If it is only a
title, it is probably not understood well enough to schedule.

**Check an entry before building it.** This file is read from, a commit
message quoted `backlog/platform-macos.md` to say macOS had no borders, five
days after macOS got borders, so a stale entry does not just sit there, it
propagates. An audit on 2026-09-18 struck nine entries across three files, and
in each case the thing was already done:

| Where | Struck | The evidence was |
| --- | --- | --- |
| macOS | 6 of 15 | the test list, and `compare_hosts.sh` agreeing on `radii=` and `transform=` |
| Testing | 2 of 12 | three AppKit test files that assert on real pixels |
| Accessibility | 1 of 5 | six `focus_*` tests on GTK |

Nothing about those nine was hard to check. What they had in common is that
nobody looked: each was written when it was true and read later as though it
still was. Running the thing an entry describes costs a minute and is the only
way to know: a grep finds code, and only running it finds behaviour.
