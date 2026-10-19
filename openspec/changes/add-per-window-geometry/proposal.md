# A window's geometry is that window's, not the active one's

## Why

`useWindow()` inside a `<Window>` reports the window the person is looking at,
not the one it is in. An app with a second window reads someone else's size,
moves the wrong window when it calls `setSize`, and re-renders when a window it
has nothing to do with is dragged.

It is wrong in the way that is hardest to notice: with one window, which is
every app today, the answer is right. It becomes wrong the first time an app
opens a second one, and then it is wrong quietly, because every number it
reports is a real number about a real window.

The cause is one seam. `core/WindowControl.h` says it plainly:

> One window, which is the honest limit. Every function here means "the
> window", because the host makes exactly one.

That stopped being true when `<Window>` shipped. Each host resolves "the
window" by asking its toolkit which one is active, which was the right answer
when there was one and is a guess now.

Three documented gaps sit on this seam, and
`docs/backlog/desktop-capabilities.md` names it as the common cause of all
three: this one, per-window menus, and a title bar that is the main window's.
This change closes it for geometry, which is the half apps already call.

## What Changes

- A window's geometry is addressed by window: `setSize`, `setPosition`,
  `center`, `setFullScreen` and `getBounds` take the window they mean.
- `useWindow()` inside a `<Window>` means *that* window, with no change to how
  an app writes it. The id is carried by React context, from the prop the
  second root is already started with.
- `windowControl`, which is callable from outside any component and therefore
  from inside no window, keeps meaning the app's own window. So does
  `useWindow()` outside a `<Window>`.
- Live bounds become the right window's: the change event carries the window it
  is about, and a hook ignores the ones that are not its own.
- Each host resolves a window id against the registry it already keeps, rather
  than asking the toolkit which window is active.

**Not in this change**, and said here rather than discovered: the size limits
stay the app's window's. They are one set of numbers in one place and Windows
answers `WM_GETMINMAXINFO` out of them, which is a message rather than a call
and cannot be given a window id without a second mechanism. `minimize`,
`toggleMaximize` and `close` stay the app's window's for now as well, because
each is a per-host call into that host's title bar rather than the shared seam.
Both are listed in tasks.md as what follows.

## Capabilities

### Modified Capabilities
- `window-geometry`

## Impact

- `native/core/WindowControl.h`: the five geometry functions take a window.
- `native/core/WindowMethods.cpp`: reads it off the call and marshals it.
- Each host's `WindowControl` implementation, and a lookup each host installs
  from the window registry its `main_*` file already keeps.
- `packages/basalt-core/src/useWindow.ts` and `Window.tsx`: the context, and
  the id on every call.
- `docs/backlog/desktop-capabilities.md`: entry 2 closes, and what is left of
  it is the two things above.
