/**
 * The half of `<KeyHandler>` that is not a component.
 *
 * Extracted so that the `View` override can use the same implementation rather
 * than a second copy of it: a view that declares `keyDownEvents` and a
 * `<KeyHandler>` are two spellings of one thing, and two spellings that drifted
 * apart would be worse than either.
 *
 * @format
 */

import {useEffect, useRef} from 'react';
import {DeviceEventEmitter, TurboModuleRegistry} from 'react-native';
import type {TurboModule} from 'react-native';

/** A combination a view claims. Modifiers left out are required to be absent. */
export interface HandledKey {
  key: string;
  altKey?: boolean;
  ctrlKey?: boolean;
  metaKey?: boolean;
  shiftKey?: boolean;
}

/** What a press reports. The same shape, which is why they compare directly. */
export type PressedKey = HandledKey;

type NativeWindowsModule = TurboModule & {
  setHandledKeys(tag: number, keys: ReadonlyArray<HandledKey>): void;
  clearHandledKeys(tag: number): void;
};

const windows = TurboModuleRegistry.get<NativeWindowsModule>('BasaltWindows');

/** The event the host emits for a press a view claimed. */
export const KEY_EVENT = 'basaltKey';

/** Something that may carry the new renderer's native tag. */
type MaybeTagged = {__nativeTag?: number} | null;

/**
 * Registers `keys` for whatever `ref` points at, and calls `onPress` when one
 * of them is pressed there.
 *
 * `keys` is compared by identity: an app that rebuilds its list every render
 * re-registers every render, which is correct and cheap. One that memoises does
 * not. `onPress` is read through a ref, so changing the handler does not
 * re-register anything -- the native side only wants the list.
 *
 * Does nothing when `keys` is absent, which is how the `View` override can call
 * it unconditionally and pay almost nothing for the views that do not use it.
 */
export function useHandledKeys(
  ref: {current: unknown},
  keys: ReadonlyArray<HandledKey> | undefined,
  onPress: ((pressed: PressedKey) => void) | undefined,
): void {
  const handler = useRef(onPress);
  handler.current = onPress;

  useEffect(() => {
    // `__nativeTag`, with two underscores: that is the new renderer's name for
    // it, and `_nativeTag` is undefined there. `<DropTarget>` learned the same.
    const tag = (ref.current as MaybeTagged)?.__nativeTag;
    if (keys == null || tag == null || windows == null) {
      return;
    }
    windows.setHandledKeys(tag, keys);
    return () => windows.clearHandledKeys(tag);
  }, [keys, ref]);

  useEffect(() => {
    if (keys == null) {
      return;
    }
    const subscription = DeviceEventEmitter.addListener(
      KEY_EVENT,
      (event: PressedKey & {tag?: number}) => {
        const tag = (ref.current as MaybeTagged)?.__nativeTag;
        // By tag, because every listener in the app hears every event: the
        // emitter is one channel and the host has already chosen which view the
        // key belongs to.
        if (tag == null || event.tag !== tag) {
          return;
        }
        handler.current?.({
          key: event.key,
          altKey: event.altKey,
          ctrlKey: event.ctrlKey,
          metaKey: event.metaKey,
          shiftKey: event.shiftKey,
        });
      },
    );
    return () => subscription.remove();
    // Whether there are keys at all decides if this listens; which keys they
    // are does not, because the host has already matched by the time it emits.
  }, [keys == null, ref]);
}
