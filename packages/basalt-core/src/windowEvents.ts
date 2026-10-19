/**
 * Whether a window event is about the window asking.
 *
 * One rule, in a module with nothing in it, because it is a decision with a
 * branch rather than a comparison: the bounds signal is one device event for
 * the whole process, and every `useWindow()` in the app hears all of them.
 *
 * No imports but a type, which is erased, so this is loadable by a test that
 * has no React and no React Native. That is the point: the rule lives in a
 * React effect otherwise, where nothing below the end-to-end suite can reach
 * it.
 *
 * @format
 */

'use strict';

import type {WindowId} from './closeRequest';

/**
 * `mine` is the window the reader is in, as the host names it.
 *
 * An event that names no window is taken as mine. That is what every event
 * meant before the field existed, and it is the safe direction: a hook that
 * ignored them would be one that silently stopped updating against an older
 * host, where the symptom is a window whose size never changes and no error
 * anywhere.
 */
export function eventIsForWindow(
  eventWindow: WindowId | null | undefined,
  mine: WindowId,
): boolean {
  return eventWindow == null || eventWindow === mine;
}
