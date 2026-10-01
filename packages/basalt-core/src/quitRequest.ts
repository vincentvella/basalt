/**
 * `useQuitRequest()` -- being asked before the application terminates.
 *
 * The same mechanism as `useCloseRequest()`, one level up, and it exists
 * because the window half is not enough: an app can guard every window it
 * owns and still lose the person's work to Cmd-Q. macOS routes quitting
 * through `applicationShouldTerminate:` and asks no window whether it minds;
 * a session ending on Linux or Windows does the same.
 *
 * ## Why a registration rather than a returned `false`
 *
 * Identical to the window case, and see src/closeRequest.ts for the long
 * version: the handler is JavaScript on another thread and the system wants a
 * synchronous yes or no, so the decision has to exist before the attempt.
 * Registering is what makes it exist -- the host then refuses the quit and
 * reports it, and the app quits itself when it is ready.
 *
 * ## One handler, not one per window
 *
 * Quitting is one event no matter how many windows are open, so this is not
 * addressed to a window and the handler takes no id. An app that registers
 * from two components gets asked twice and can quit twice, which is
 * harmless -- the second `quit()` arrives at a process that is already
 * going -- but is not the intended shape. Register it once, near the top.
 *
 * ## What each desktop means by "quit"
 *
 * On macOS this is Cmd-Q, the Quit menu item, and logging out. On Windows and
 * Linux there is no application-level quit gesture to intercept -- an app ends
 * when its last window closes, which is a window close and `useCloseRequest`'s
 * business -- so what this catches there is the session ending: logout,
 * shutdown, reboot.
 *
 * @format
 */

import * as React from 'react';
import type {TurboModule} from 'react-native';
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

/**
 * What the handler is given: call it to let the quit through. It stops
 * intercepting first, so the quit it then asks for is not refused by the
 * registration that produced the question.
 */
export type AllowQuit = () => void;

export type QuitRequestHandler = (quit: AllowQuit) => void;

type NativeWindowsModule = TurboModule & {
  interceptQuit?(intercepted: boolean): void;
  quit?(): void;
};

const NativeWindows = TurboModuleRegistry.get<NativeWindowsModule>('BasaltWindows');

// Must match kQuitRequestedEvent in native/core/WindowsModule.h.
const QUIT_REQUESTED_EVENT = 'basaltQuitRequested';

/** Whether this platform can be asked at all. */
export const isSupported = NativeWindows?.interceptQuit != null;

/**
 * Ask to be asked before the application quits.
 *
 * Pass null to stop asking. The handler is read through a ref, so an app that
 * passes a new closure every render does not re-register -- what re-runs the
 * effect is whether there is a handler at all, plus `deps`.
 */
export function useQuitRequest(
  handler: QuitRequestHandler | null | undefined,
  deps: ReadonlyArray<unknown> = [],
): void {
  const handlerRef = React.useRef(handler);
  handlerRef.current = handler;

  const wanted = handler != null;

  React.useEffect(() => {
    if (!isSupported || !wanted) {
      return;
    }
    // `isSupported` above is what proves these are here; it is computed at
    // module scope from the same optional call, which the compiler cannot
    // follow.
    NativeWindows!.interceptQuit!(true);

    const subscription = DeviceEventEmitter.addListener(QUIT_REQUESTED_EVENT, () => {
      handlerRef.current?.(() => {
        NativeWindows!.interceptQuit!(false);
        NativeWindows!.quit?.();
      });
    });

    return () => {
      // Stop intercepting on the way out, for the reason the window half
      // does: an app that stopped listening and went on refusing every quit
      // would be a process that cannot be quit at all, which is worse than
      // the bug this prevents.
      NativeWindows!.interceptQuit!(false);
      subscription.remove();
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [wanted, ...deps]);
}

/** Ends the application, asking nobody. */
export function quit(): void {
  NativeWindows?.quit?.();
}
