/**
 * Which window a React tree is running in.
 *
 * A second window is a second surface and therefore a second React root, and
 * the host starts that root with `basaltWindowId` in its initial props. So the
 * tree inside a window already knows which one it is; what was missing was a
 * way for a hook deep inside it to ask. This is that: `<Window>` provides it
 * around the children it renders, and `useWindow()` reads it.
 *
 * Zero is the default, and is not a window id -- the first window's is 1 --
 * so a tree outside any `<Window>` reads "no particular window" and means the
 * one the app started in. That is also what `windowControl` sends, because
 * code outside a component is inside no tree at all.
 *
 * In a module of its own so that `Window.tsx` and `useWindow.ts` can both
 * reach it without importing each other.
 *
 * @format
 */

'use strict';

import * as React from 'react';

import type {WindowId} from './closeRequest';

/** The window a call means when it names none: the one the app started in. */
export const APP_WINDOW: WindowId = 0;

export const WindowIdContext: React.Context<WindowId> =
  React.createContext<WindowId>(APP_WINDOW);

/**
 * The window the calling tree is in, or `APP_WINDOW` outside one.
 *
 * A hook rather than exporting the context, because what a caller wants is
 * the answer and not the machinery, and because the default has a meaning
 * worth keeping in one place.
 */
export function useWindowId(): WindowId {
  return React.useContext(WindowIdContext);
}
