# Tasks

## 1. The shared decision

- [x] `core/WindowControl.h`: `windowBounds`, `setWindowSize`,
      `setWindowPosition`, `centerWindow` and `setWindowFullScreen` take the
      window they mean. Zero keeps meaning the app's own, which is what a call
      that names none sends
- [x] The bounds cache becomes one per window, behind the lock it already has,
      and an unknown window reads zeroes the way a window that does not exist
      yet does
- [x] `notifyWindowBoundsChanged(window)` and the listener carry the id, so the
      event says which window moved
- [x] `core/WindowMethods.cpp`: read a trailing optional id off every geometry
      call, and send it on the event
- [x] Unit tests in the shared suite: a call with no id means zero, a cache
      read for a window nobody has reported answers zeroes rather than another
      window's numbers, and two windows' bounds do not overwrite each other

## 2. The hosts

- [x] AppKit: `setWindowLookup` installed from `main_appkit.mm`'s registry,
      `windowFor(id)` falling back to `appWindow()`, and the five functions
      using it. Tests: a lookup that answers, one that does not, and zero
- [x] GTK: the same, over `gtk_window_get_toplevels`' fallback
- [x] Win32: the same, over the HWND registry. No local build here; CI is what
      says it compiles and the unit suite is what says it behaves
- [x] Each host reports the window with its bounds change, from the per-window
      handler it already has

## 3. JavaScript

- [x] `Window.tsx`: a context carrying the window's id, provided around the
      second root's children. Not `basaltWindowId`, which turned out to be a
      JavaScript counter keying the content map: the host adds the surface id
      as `kWindowSurfaceIdProp` and that is what the context carries
- [x] `useWindow.ts`: read it, send it on every call, and ignore a bounds event
      for another window. `windowControl` and a `useWindow()` outside any
      `<Window>` keep meaning the app's window
- [x] Types: the id is `WindowId` from closeRequest.ts, not a second alias
- [x] `scripts/test_platform_js.js`: which window a bounds event is about.
      The rule moved to `windowEvents.ts`, a module with no imports but a
      type, because inside a React effect nothing below the end-to-end suite
      can reach it

## 4. Proving it

- [x] `e2e/windows.tsx` grows a second window that reports its own bounds, and
      the scenario asserts the two windows report *different* sizes -- which is
      the assertion that fails today
- [x] Sabotage. Not the one planned: making each host's lookup ignore its
      argument leaves the scenario passing, because in an app with one process
      the window being acted on is also the active one, so the fallback
      answers correctly by accident. What does fail it is giving the second
      tree the content-map counter again instead of the surface id, which is
      the bug this change is about: both windows then report 900x732. The
      scenario's docstring says which half it cannot prove

## 5. Writing it down

- [x] `docs/backlog/desktop-capabilities.md` entry 2 closes, leaving the two
      things this does not do
- [x] The `:::note` on `website/docs/reference/windows.mdx` goes, and the page
      says which window a hook means
- [x] `core/WindowControl.h`'s "one window, which is the honest limit" stops
      being the limit

## What follows, and is not this

- [ ] `minimize`, `toggleMaximize` and `close` are per-host calls into each
      host's title bar; they take a window next
- [ ] The size limits stay the app's window's while `WM_GETMINMAXINFO` has no
      way to be told which window it is answering for
- [ ] Per-window menus and a per-window title bar, which are the other two
      things on this seam
