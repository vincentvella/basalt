# Design

## What a window is called

Its surface id, which is what `core/WindowHost.h` already decided:

> One window, one surface, one React root. [...] It is also why the id a window
> is identified by *is* its surface id: there is no second numbering to keep in
> step.

So there is nothing to invent and nothing to map. `<Window>` starts its second
root with `props: {basaltWindowId}`, which is that surface id;
`closeRequest.ts` already speaks in the same ids, and `getWindows()` already
answers them main window first.

**Zero means "the app's window".** It is not a surface id -- the main window's
is 1 -- so it is free, and it is what a call that names no window sends. That
keeps `windowControl` working from outside any component, where there is no
window to be in, and keeps a host that has not been taught the lookup yet
behaving exactly as it does today.

## Where the id rides

**A trailing optional argument**, not a leading one: `setSize(width, height,
id?)`, `getBounds(id?)`.

Leading reads better and would be the choice on a clean slate. Trailing is
chosen because it is the shape that lets this land in pieces. Every host's
argument check is "at least N numbers", so a host that has not been changed
ignores an id it was sent rather than misreading a size as one, and a JavaScript
bundle that does not send one keeps working against a host that expects it.
This is a published package whose native half is compiled from source by the app
that installs it; the two halves are not always rebuilt in the same breath.

## How a host finds the window

Each host already keeps the registry. `main_appkit.mm`, `main_gtk.cpp` and
`main_win32.cpp` each have a `HostWindow` list and a `windowFor(surfaceId)`,
because opening and closing a second window needs one.

What they do not have is a way for the *window module* to ask: the module is
compiled into each host's library, and the registry lives in the executable's
`main_*` file. So each host's window-control file grows a lookup it owns and the
executable installs at startup:

    void setWindowLookup(std::function<NSWindow *(SurfaceId)>);   // AppKit
                                     GtkWindow *                  // GTK
                                     HWND                         // Win32

and `windowFor(id)` which uses it, falling back to today's active-window rule
when the id is zero or the lookup is not installed. The fallback is what keeps
every test binary and both mount harnesses working: none of them has a window
registry, and none of them should grow one to keep compiling.

A `std::function` rather than a plain function pointer because the AppKit and
GTK hosts both capture their `gHost` singleton by reference.

## Live bounds, and which window moved

`setWindowBoundsListener` hands the module one callback for the process, and the
module turns it into a device event. The event grows the window's id, and
`useWindow()` ignores an event for a window that is not its own.

The host already knows which window moved -- `notifyWindowBoundsChanged()` is
called from a per-window resize or move handler -- but the *signature* throws it
away. It grows the id, and the bounds cache becomes a cache per window rather
than one set of numbers.

That cache is the subtle half. `lastKnownSurfaceSize` and the bounds cache are
read from any thread, so the per-window version is a small map behind the same
lock rather than a field, and a read for an unknown window answers zeroes
exactly as a read before the first window does today.

## Why not give the module a handle instead

The alternative is what the `WindowControl.h` comment imagined: "the module
grows a handle". One module instance per window, each holding its own window,
and JavaScript gets the right one by asking for the right module.

TurboModules are not addressable that way. `TurboModuleRegistry.get('BasaltWindow')`
is a name, the host's module provider returns one instance per name, and nothing
in the call tells it which surface is calling. Giving each window its own module
would mean a name per window -- `BasaltWindow:2` -- invented in JavaScript and
parsed in the host, which is a second numbering of exactly the kind the surface
id was chosen to avoid.

So the id travels as an argument, which is the thing a TurboModule call can
carry.
