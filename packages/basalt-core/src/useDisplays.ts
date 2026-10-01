/**
 * What screens the desktop has, where they are, and how dense they are.
 *
 * React Native's `Dimensions` reports the *window*, which is the right answer
 * to a different question. An app placing a window, or deciding how much it
 * can show, is asking about the screen it is on and the ones beside it.
 *
 * ## What each desktop will and will not say
 *
 * Bounds and scale factor: everywhere. The work area -- what is left after a
 * menu bar, taskbar or dock -- comes from macOS and Windows, and GTK 4 has no
 * way to ask, so it reports the full bounds there. The pointer's position
 * comes from macOS and Windows and never from GTK.
 *
 * None of those are gaps waiting to be filled. `gdk_monitor_get_workarea`,
 * `gdk_display_get_primary_monitor` and reading the pointer were all in GTK 3
 * and were removed: Wayland has no protocol a client can use for any of them.
 * See native/gtk/GtkWindowControl.cpp.
 *
 * So an app that needs the work area should treat "equal to bounds" as the
 * answer rather than as a failure, and one that needs the pointer should check
 * `known`.
 *
 * @format
 */

import * as React from 'react';
import type {TurboModule} from 'react-native';
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

/** One display, in the same logical pixels and coordinates windows use. */
export type Display = {
  x: number;
  y: number;
  width: number;
  height: number;
  /** What is left after the desktop's furniture; equal to the bounds on GTK. */
  workX: number;
  workY: number;
  workWidth: number;
  workHeight: number;
  /** Physical pixels per logical pixel. Never zero. */
  scaleFactor: number;
  primary: boolean;
};

export type PointerPosition = {
  x: number;
  y: number;
  /** False where the desktop will not say -- always, on GTK. */
  known: boolean;
};

type NativeWindowsModule = TurboModule & {
  getDisplays?(): Display[] | null;
  getPointerPosition?(): Promise<PointerPosition>;
};

const NativeWindows = TurboModuleRegistry.get<NativeWindowsModule>('BasaltWindows');

// Must match kDisplaysChangedEvent in native/core/WindowsModule.h.
const DISPLAYS_CHANGED_EVENT = 'basaltDisplaysChanged';

/** Whether this platform can be asked at all. */
export const isSupported = NativeWindows?.getDisplays != null;

/**
 * Every display, primary first.
 *
 * Synchronous, because the host keeps the list up to date rather than the
 * platform asking the toolkit on demand -- see core/WindowControl.h. Empty
 * before the host has reported anything, which an app can distinguish from
 * "no displays" only by there being no such desktop.
 */
export function displays(): Display[] {
  const found = NativeWindows?.getDisplays?.();
  return Array.isArray(found) ? found : [];
}

/** Where the pointer is. A promise: nothing reports the pointer moving. */
export function pointerPosition(): Promise<PointerPosition> {
  return (
    NativeWindows?.getPointerPosition?.() ??
    Promise.resolve({x: 0, y: 0, known: false})
  );
}

/**
 * The displays, kept current.
 *
 * Re-reads whenever a monitor is plugged in, unplugged or rearranged. The
 * event carries no payload for a reason -- several changes can arrive
 * together, and re-reading is the only way to be right about all of them.
 */
export function useDisplays(): Display[] {
  const [current, setCurrent] = React.useState<Display[]>(displays);

  React.useEffect(() => {
    if (!isSupported) {
      return;
    }
    // Once on mount as well as on every change: the list may have changed
    // between the first render and this effect.
    setCurrent(displays());

    const subscription = DeviceEventEmitter.addListener(DISPLAYS_CHANGED_EVENT, () => {
      setCurrent(displays());
    });
    return () => subscription.remove();
  }, []);

  return current;
}

/** The primary display, or null on a platform with none to report. */
export function primaryDisplay(): Display | null {
  const found = displays();
  return found.find(display => display.primary) ?? found[0] ?? null;
}
