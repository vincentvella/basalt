# Tasks

## 1. The seam

- [x] Add a display model and lookup to `core/WindowControl.h`, plus
      `lastKnownDisplays()` -- the list is cached the way the window's bounds
      are, because a TurboModule method runs on the JavaScript thread and
      there is no synchronous hop to the UI thread in this codebase
- [x] Add a change listener, following the window bounds listener

## 2. Hosts

- [x] GTK: `GdkDisplay` and its monitors, plus the monitors-changed signal --
      which is the monitor list's `items-changed`, GTK 4 having no
      monitors-changed on `GdkDisplay`.

      **Three things GTK 4 will not say**, all of them removed rather than
      overlooked: `gdk_monitor_get_workarea`, `gdk_display_get_primary_monitor`
      and any way to read the pointer were GTK 3 APIs, and Wayland has no
      protocol for a work area a client can read, no notion of a primary
      output, and no pointer position outside the client's own surfaces. So
      the work area is reported as the full bounds, the first monitor as
      primary, and the pointer as unknown -- each a truthful answer to a
      question this desktop does not answer
- [x] AppKit: `NSScreen.screens` and
      `applicationDidChangeScreenParameters:`. The bounds are flipped onto a
      top-left origin with the same anchor `windowBounds()` uses -- two
      conventions in one API would be invisible until somebody put a monitor
      above their first one
- [x] Win32: `EnumDisplayMonitors` and `WM_DISPLAYCHANGE`. `MONITORINFOEXW`
      carries both rectangles, so the work area costs nothing there; the
      scale factor is `GetDpiForMonitor` over 96, which needed `shcore`
      linked and `shellscalingapi.h` included after `windows.h`

## 3. JavaScript and tests

- [x] `useDisplays()`, `displays()`, `primaryDisplay()` and
      `pointerPosition()`, exported from `basalt-core`. The pointer is
      the one promise: nothing reports it moving, so there is nothing to cache
- [x] Demo in `js/displays.js`, scenario asserting at least one display,
      exactly one primary, and a scale factor that is a ratio rather than a
      DPI -- checked against that bug by making AppKit report 192


## 4. What building it found

- [x] Priming the cache from `windowDidResize` is not priming it. The first
      version did, and an app that never resized its window was told the
      desktop had no screens -- `displays 0` in the tree. It primes where the
      bounds cache does, at the surface start.
- [x] The scenario counted log lines rather than displays. The demo reports
      the list whenever it changes, and it changes once between the first
      render and the effect that re-reads it, so one primary display was
      counted twice. It reads the tree now, which has one entry per display
      and not one per render.
