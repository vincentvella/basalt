/**
 * The window: how big it is, where it is, and what it is doing.
 *
 *   const window = useWindow();
 *
 *   window.setSize(900, 700);
 *   window.setPosition(100, 100);
 *   window.center();
 *   window.setFullScreen(true);
 *   window.minimize();
 *   window.toggleMaximize();
 *   window.close();
 *
 *   const {width, height, x, y, fullScreen, maximized} = window.bounds;
 *
 * React Native has no API for any of this: on a phone there is one window, it
 * is the screen, and nothing an app says would change it. On a desktop it is
 * the first thing an app wants after it has drawn something.
 *
 * ## `bounds` is live
 *
 * It re-renders when the window moves, is resized, or changes state, because
 * the alternative -- a `getBounds()` an app calls and caches -- is an app whose
 * layout is right until somebody drags a corner. The host emits the change; see
 * native/core/WindowMethods.h.
 *
 * Reading it before there is a window gives zeroes rather than null, so that
 * `const {width} = window.bounds` never throws during a first render.
 *
 * ## What the platforms will not do
 *
 * `setPosition` and `center` do nothing on Linux, and `bounds.x` and `bounds.y`
 * are always zero there. This is not unimplemented: GTK4 removed
 * `gtk_window_move` and Wayland has no equivalent, because a client has no idea
 * where its window is and no say in it. Both display servers centre an unplaced
 * window by default, so an app that never moves its window already gets what
 * `center()` would have asked for.
 *
 * Every other platform difference is spelling. `setSize` is a request on all
 * three -- a tiling window manager may ignore it -- which is why `bounds`
 * reports what happened rather than what was asked for.
 *
 * ## How big it may be, and where it sits
 *
 *   window.setMinimumSize(400, 300);
 *   window.setMaximumSize(1200, 900);   // 0 in either direction clears a limit
 *   window.setResizable(false);
 *   window.setAlwaysOnTop(true);
 *
 * Two of these do nothing on Linux, and that is settled rather than pending:
 * GTK4 removed `gtk_window_set_geometry_hints` and `gtk_window_set_keep_above`
 * because Wayland has no protocol for either. So rather than have an app read
 * this paragraph, `window.capabilities` answers:
 *
 *   {position, minimumSize, maximumSize, resizable, alwaysOnTop}
 *
 * all true on macOS and Windows; on Linux, `minimumSize` and `resizable`. An
 * app can put its "always on top" switch away on the desktop that has no such
 * thing, which is better than a switch that lies.
 *
 * ## Refusing to close
 *
 *   useCloseRequest(close => {
 *     if (unsaved) {
 *       setAsking(true);
 *     } else {
 *       close();
 *     }
 *   });
 *
 * A window with a close handler stops closing on its own: every attempt -- its
 * close button, Cmd-W, Alt+F4, the window manager -- is refused and reported,
 * and the window goes when the app says so by calling the `close` it was
 * handed. Which is the point: there is nowhere else to put "are you sure".
 *
 * On macOS this covers closing the window and not Cmd-Q, which terminates the
 * application without asking any window whether it minds.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import type {TurboModule} from 'react-native';
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

import {closeWindowById, mainWindowId, useCloseRequestFor} from './closeRequest';
import type {CloseRequestHandler, WindowId} from './closeRequest';
import {APP_WINDOW, useWindowId} from './windowContext';
import {eventIsForWindow} from './windowEvents';

export type {CloseRequestHandler, WindowId} from './closeRequest';

/** Where the window is and how big, plus the two states that are not sizes. */
export type WindowBounds = {
  x: number;
  y: number;
  width: number;
  height: number;
  fullScreen: boolean;
  maximized: boolean;
};

/**
 * What this desktop actually does, which is not the same list everywhere.
 *
 * All false where there is no window module, so an app that checks before
 * offering a control gets the right answer rather than an exception.
 */
export type WindowCapabilities = {
  position: boolean;
  minimumSize: boolean;
  maximumSize: boolean;
  resizable: boolean;
  alwaysOnTop: boolean;
};

type NativeWindowModule = TurboModule & {
  // The window each of these is about, trailing and optional: zero, or
  // nothing at all, means the window the app started in. See the change
  // openspec/changes/add-per-window-geometry for why it rides there rather
  // than first.
  setSize?(width: number, height: number, window?: WindowId): void;
  setPosition?(x: number, y: number, window?: WindowId): void;
  center?(window?: WindowId): void;
  setFullScreen?(fullScreen: boolean, window?: WindowId): void;
  minimize?(): void;
  toggleMaximize?(): void;
  close?(): void;
  setMinimumSize?(width: number, height: number): void;
  setMaximumSize?(width: number, height: number): void;
  setResizable?(resizable: boolean): void;
  setAlwaysOnTop?(alwaysOnTop: boolean): void;
  getBounds?(window?: WindowId): WindowBounds | null;
  getCapabilities?(): WindowCapabilities | null;
};

const NativeWindow = TurboModuleRegistry.get<NativeWindowModule>('BasaltWindow');

// Must match kWindowBoundsEvent in native/core/WindowMethods.h.
const BOUNDS_EVENT = 'basaltWindowBoundsChanged';

const NO_BOUNDS: WindowBounds = Object.freeze({
  x: 0,
  y: 0,
  width: 0,
  height: 0,
  fullScreen: false,
  maximized: false,
});

// What this desktop actually does, which is not the same list everywhere.
//
// All false on a host with no window module, so an app that checks before
// offering a control gets the right answer rather than an exception.
const NO_CAPABILITIES: WindowCapabilities = Object.freeze({
  position: false,
  minimumSize: false,
  maximumSize: false,
  resizable: false,
  alwaysOnTop: false,
});

function readCapabilities(): WindowCapabilities {
  if (NativeWindow?.getCapabilities == null) {
    return NO_CAPABILITIES;
  }
  return NativeWindow.getCapabilities() ?? NO_CAPABILITIES;
}

// Asked once. These are five booleans a platform knows at compile time, not a
// question for the window manager, and re-reading them on every render would be
// a JSI call per frame for an answer that cannot change.
const capabilities = readCapabilities();

function readBounds(window: WindowId): WindowBounds {
  if (NativeWindow?.getBounds == null) {
    return NO_BOUNDS;
  }
  const bounds = NativeWindow.getBounds(window);
  return bounds != null ? bounds : NO_BOUNDS;
}

/**
 * The same calls, bound to one window.
 *
 * A factory rather than an id threaded through every call site, because
 * `useWindow()` returns this object and an app holds onto it: binding the
 * window once is what keeps `const {setSize} = useWindow()` working, which is
 * how people write it.
 *
 * Every call is a no-op on a host with no window module, so the same code runs
 * on a platform that has not implemented one.
 */
function controlFor(window: WindowId) {
  return Object.freeze({
    setSize(width: number, height: number): void {
      NativeWindow?.setSize?.(width, height, window);
    },
    setPosition(x: number, y: number): void {
      NativeWindow?.setPosition?.(x, y, window);
    },
    center(): void {
      NativeWindow?.center?.(window);
    },
    setFullScreen(fullScreen?: boolean): void {
      NativeWindow?.setFullScreen?.(fullScreen === true, window);
    },
    // Minimising and maximising are the app's own window's, whichever window
    // asks. Both are a per-host call into that host's title bar rather than
    // the shared geometry seam, and taking a window is what follows this
    // change; see openspec/changes/add-per-window-geometry/tasks.md.
    minimize(): void {
      NativeWindow?.minimize?.();
    },
    toggleMaximize(): void {
      NativeWindow?.toggleMaximize?.();
    },
    /**
     * Closes this window.
     *
     * A second window closes through the windows module, which has taken an
     * id since it was written, rather than through `BasaltWindow.close()`,
     * which is the app's own window's. Without this split a preview window
     * calling `close()` on itself closed the window behind it, which is the
     * worst version of the bug this whole change is about.
     */
    close(): void {
      if (window !== APP_WINDOW) {
        closeWindowById(window);
        return;
      }
      NativeWindow?.close?.();
    },
    // How big it may be is one set of numbers for the application, which is
    // the restriction core/WindowControl.h records: Windows answers
    // WM_GETMINMAXINFO out of them, and a message cannot be told which window
    // it is about.
    setMinimumSize(width: number, height: number): void {
      NativeWindow?.setMinimumSize?.(width, height);
    },
    setMaximumSize(width: number, height: number): void {
      NativeWindow?.setMaximumSize?.(width, height);
    },
    setResizable(resizable?: boolean): void {
      NativeWindow?.setResizable?.(resizable !== false);
    },
    setAlwaysOnTop(alwaysOnTop?: boolean): void {
      NativeWindow?.setAlwaysOnTop?.(alwaysOnTop === true);
    },
    getBounds(): WindowBounds {
      return readBounds(window);
    },
    getCapabilities(): WindowCapabilities {
      // The cached answer, so that the imperative half and the hook cannot
      // disagree about a thing that does not change.
      return capabilities;
    },
  });
}

/**
 * The imperative half, usable outside a component.
 *
 * The app's own window, because code outside a component is inside no window:
 * there is no React tree to read a context from. An app that wants a second
 * window's geometry from outside its tree has its id and can say so.
 */
export const windowControl = controlFor(APP_WINDOW);

/**
 * Be asked before this window closes.
 *
 * Pass null or nothing to stop being asked, which is also what unmounting does.
 * See the header above, and `<Window onCloseRequest>` for a second window.
 */
export function useCloseRequest(handler?: CloseRequestHandler | null): void {
  useCloseRequestFor(mainWindowId, () => windowControl.close(), handler, []);
}

export type WindowControl = typeof windowControl;

/** What the hook returns: the imperative surface, plus live state. */
export type UseWindow = WindowControl & {
  bounds: WindowBounds;
  capabilities: WindowCapabilities;
};

export function useWindow(): UseWindow {
  // The window this tree is in, which is the window every call below means.
  // `APP_WINDOW` outside any `<Window>`, which is the app's own.
  const window = useWindowId();
  const [bounds, setBounds] = React.useState(() => readBounds(window));

  React.useEffect(() => {
    // Read again on mount as well as subscribing. The window may have been
    // resized between the first render and this effect -- which on a host that
    // opens its window and mounts into it is the common case, not the rare one.
    setBounds(readBounds(window));

    // Which id the events about this window carry. Asked once per
    // subscription rather than per event: `mainWindowId()` is a call into the
    // host, and a resize is a stream.
    //
    // The hook holds zero for the app's own window and the host names it by
    // its real id, so the two have to be brought together somewhere, and this
    // is the cheapest place.
    const mine = window === APP_WINDOW ? mainWindowId() : window;

    const subscription = DeviceEventEmitter.addListener(
      BOUNDS_EVENT,
      (next: (WindowBounds & {window?: WindowId}) | null) => {
        // Only this window's. The event is one signal for the process, so an
        // app in a preview window would otherwise re-render -- with the other
        // window's numbers -- every time somebody dragged the main one. The
        // rule, including what an event that names no window means, is
        // windowEvents.ts, where a test can reach it.
        if (!eventIsForWindow(next?.window, mine)) {
          return;
        }
        setBounds(next ?? NO_BOUNDS);
      },
    );
    return () => subscription.remove();
  }, [window]);

  return React.useMemo(
    () => ({...controlFor(window), bounds, capabilities}),
    [bounds, window],
  );
}
