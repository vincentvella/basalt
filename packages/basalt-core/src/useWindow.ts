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

import {mainWindowId, useCloseRequestFor} from './closeRequest';
import type {CloseRequestHandler} from './closeRequest';

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
  setSize?(width: number, height: number): void;
  setPosition?(x: number, y: number): void;
  center?(): void;
  setFullScreen?(fullScreen: boolean): void;
  minimize?(): void;
  toggleMaximize?(): void;
  close?(): void;
  setMinimumSize?(width: number, height: number): void;
  setMaximumSize?(width: number, height: number): void;
  setResizable?(resizable: boolean): void;
  setAlwaysOnTop?(alwaysOnTop: boolean): void;
  getBounds?(): WindowBounds | null;
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

function readBounds(): WindowBounds {
  if (NativeWindow?.getBounds == null) {
    return NO_BOUNDS;
  }
  const bounds = NativeWindow.getBounds();
  return bounds != null ? bounds : NO_BOUNDS;
}

/**
 * The imperative half, usable outside a component.
 *
 * Every call is a no-op on a host with no window module, so the same code runs
 * on a platform that has not implemented one.
 */
export const windowControl = Object.freeze({
  setSize(width: number, height: number): void {
    NativeWindow?.setSize?.(width, height);
  },
  setPosition(x: number, y: number): void {
    NativeWindow?.setPosition?.(x, y);
  },
  center(): void {
    NativeWindow?.center?.();
  },
  setFullScreen(fullScreen?: boolean): void {
    NativeWindow?.setFullScreen?.(fullScreen === true);
  },
  minimize(): void {
    NativeWindow?.minimize?.();
  },
  toggleMaximize(): void {
    NativeWindow?.toggleMaximize?.();
  },
  close(): void {
    NativeWindow?.close?.();
  },
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
    return readBounds();
  },
  getCapabilities(): WindowCapabilities {
    // The cached answer, so that the imperative half and the hook cannot
    // disagree about a thing that does not change.
    return capabilities;
  },
});

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
  const [bounds, setBounds] = React.useState(readBounds);

  React.useEffect(() => {
    // Read again on mount as well as subscribing. The window may have been
    // resized between the first render and this effect -- which on a host that
    // opens its window and mounts into it is the common case, not the rare one.
    setBounds(readBounds());

    const subscription = DeviceEventEmitter.addListener(
      BOUNDS_EVENT,
      (next: WindowBounds | null) => {
        setBounds(next ?? NO_BOUNDS);
      },
    );
    return () => subscription.remove();
  }, []);

  return React.useMemo(() => ({...windowControl, bounds, capabilities}), [bounds]);
}
