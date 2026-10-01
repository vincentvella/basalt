/**
 * Being asked before a window closes.
 *
 * The shared half of `useCloseRequest()` and `<Window onCloseRequest>`: they
 * are the same mechanism pointed at two different windows, and the only thing
 * that differs is which id and how it closes.
 *
 * ## Why a registration rather than a returned `false`
 *
 * Electron spells this `event.preventDefault()` in a `close` handler, and can,
 * because its handler runs in the main process and the answer is there the
 * moment the event is raised. Here the handler is JavaScript on another thread
 * and the window manager wants a synchronous yes or no -- so the decision has
 * to exist *before* the attempt. Registering a handler is what makes it exist:
 * the host then refuses every close and reports the attempt, and the app closes
 * the window itself when it is ready.
 *
 * The cost is that a window with a handler never closes on its own, which is
 * why the handler is given the thing that does.
 *
 * @format
 */

import * as React from 'react';
import type {TurboModule} from 'react-native';
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

/** A window's id, as the host hands them out. */
export type WindowId = number;

/**
 * What the handler is given: call it to let the close through. It stops
 * intercepting first, so the close it then asks for is not refused by the
 * registration that produced the question.
 */
export type AllowClose = () => void;

export type CloseRequestHandler = (close: AllowClose) => void;

type NativeWindowsModule = TurboModule & {
  interceptClose?(id: WindowId, intercepted: boolean): void;
  getWindows?(): WindowId[] | null;
};

const NativeWindows = TurboModuleRegistry.get<NativeWindowsModule>('BasaltWindows');

// Must match kWindowCloseRequestedEvent in native/core/WindowsModule.h.
const CLOSE_REQUESTED_EVENT = 'basaltWindowCloseRequested';

/** Whether this platform can be asked at all. */
export const isSupported = NativeWindows?.interceptClose != null;

/**
 * The window the app started in.
 *
 * Asked rather than assumed: `getWindows()` answers main window first, which is
 * the same ordering the host keeps its records in. The fallback is the constant
 * every host uses for it, for a platform with no windows module -- where the
 * whole thing is a no-op anyway.
 */
export function mainWindowId(): WindowId {
  const open = NativeWindows?.getWindows?.();
  return Array.isArray(open) && open.length > 0 ? open[0] : 1;
}

/**
 * Ask to be asked, and hear about it.
 *
 * `getId` is a function rather than a number because a second window's id
 * arrives from a promise: the hook may run before the window exists. Returning
 * null means "not yet", and `deps` is what re-runs this once it is.
 *
 * `close` is what the handler is handed. It stops intercepting first, so that
 * the close it then asks for is not refused by the registration that produced
 * the question -- an app that answers "yes" once should not be asked again.
 */
export function useCloseRequestFor(
  getId: () => WindowId | null | undefined,
  close: (id: WindowId) => void,
  handler: CloseRequestHandler | null | undefined,
  deps: ReadonlyArray<unknown>,
): void {
  const handlerRef = React.useRef(handler);
  handlerRef.current = handler;

  // In a ref for the same reason, and it matters less only because both callers
  // pass something that closes over nothing. Reading it through a ref is what
  // keeps that from being load-bearing.
  const closeRef = React.useRef(close);
  closeRef.current = close;

  // Whether there is a handler at all, rather than which one. An app passes a
  // new closure every render and re-registering on each would be a subscription
  // torn down and rebuilt for nothing.
  const wanted = handler != null;

  React.useEffect(() => {
    if (!isSupported || !wanted) {
      return;
    }
    const id = getId();
    if (id == null) {
      return;
    }
    // `isSupported` above is what proves these are here; it is computed at
    // module scope from the same optional call, which the compiler cannot
    // follow.
    NativeWindows!.interceptClose!(id, true);

    const subscription = DeviceEventEmitter.addListener(
      CLOSE_REQUESTED_EVENT,
      (requestedId: WindowId) => {
        if (requestedId !== id) {
          return;
        }
        handlerRef.current?.(() => {
          NativeWindows!.interceptClose!(id, false);
          closeRef.current(id);
        });
      },
    );

    return () => {
      // Stop intercepting on the way out. A window whose app stopped listening
      // and went on refusing every close would be a window that cannot be
      // closed at all, which is a worse bug than the one this prevents.
      NativeWindows!.interceptClose!(id, false);
      subscription.remove();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [wanted, ...deps]);
}
