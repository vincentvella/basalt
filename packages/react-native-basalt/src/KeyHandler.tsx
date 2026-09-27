/**
 * `<KeyHandler>` -- a view that declares keyboard shortcuts.
 *
 * `onKeyPress` exists on a `<TextInput>` and a `<View>` had nothing, so an app
 * could not bind a shortcut to anything it draws. On a phone that is no loss; on
 * a desktop it is most of the interface.
 *
 * ## The names are a browser's
 *
 * `key` is what `KeyboardEvent.key` reports: `'a'`, `'A'`, `' '`, `'ArrowLeft'`,
 * `'Escape'`. react-native-macos and react-native-windows both use these, so a
 * binding means the same thing on every platform an app might run on. Case
 * follows the character, as in a browser: Shift+A is `'A'` *and* has `shiftKey`.
 *
 * ## Why the keys are declared and not just listened for
 *
 * Because a key nothing handled has to go on -- to the menu, to a focused text
 * field, to a scroll view that scrolls on arrows -- and a key that was handled
 * must not. The host decides that synchronously, inside `keyDown:`, and cannot
 * ask JavaScript at that moment. So the list is what answers, and a combination
 * absent from it is one this view will never be told about.
 *
 * ## Why not `nativeID`, which is how `<DropTarget>` does it
 *
 * Two reasons, both about this feature rather than taste. The list is long and it
 * changes -- an editor registers twenty-odd combinations and adds to them as
 * panes mount -- and `nativeID` holds one thing, which `<DropTarget>` already
 * documents as a limit. A timeline that accepts dropped media *and* has shortcuts
 * is an ordinary thing to want, and two features competing for one string would
 * make it impossible.
 *
 * So this registers by the view's tag and unregisters on unmount. The failure
 * mode that trade brings -- silently stopping after a re-render -- is what
 * `keys` being in the effect's dependencies is for, and what a scenario asserts.
 */
import React, {useEffect, useRef} from 'react';
import {
  DeviceEventEmitter,
  TurboModuleRegistry,
  View,
  type ViewProps,
} from 'react-native';

const KEY_EVENT = 'basaltKey';

// Constructed at import, not called at import. Its constructor is what registers
// the host-side listener, and without that no key ever reaches JavaScript --
// which is the same trap `<DropTarget>` documents and hit first.
const windows = TurboModuleRegistry.get('BasaltWindows') as unknown as {
  setHandledKeys(tag: number, keys: ReadonlyArray<HandledKey>): void;
  clearHandledKeys(tag: number): void;
} | null;

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

export type KeyHandlerProps = ViewProps & {
  /** The combinations this view handles. Anything else passes through. */
  keys: ReadonlyArray<HandledKey>;
  onKeyDown?: (pressed: PressedKey) => void;
  /**
   * Whether to take keyboard focus when it mounts, which defaults to true.
   *
   * A key press arrives at whatever has focus and is offered to that view and
   * its ancestors, so a `<KeyHandler>` nothing ever focuses is never asked. For
   * a window-level shortcut holder -- the common case, and the reason this
   * exists -- that would mean no shortcut ever fires, which is the wrong default
   * to have. Set it false for a handler inside a pane that should only be live
   * while that pane has focus.
   */
  focusOnMount?: boolean;
};

export function KeyHandler({
  keys,
  onKeyDown,
  focusOnMount = true,
  children,
  ...viewProps
}: KeyHandlerProps): React.ReactElement {
  const ref = useRef<React.ComponentRef<typeof View> | null>(null);
  // The handler in a ref, so changing it does not re-register the keys: the
  // native side only needs the list, and re-registering on every render that
  // makes a new closure would be churn for nothing.
  const handler = useRef(onKeyDown);
  handler.current = onKeyDown;

  useEffect(() => {
    // `__nativeTag`, with two underscores: that is the new renderer's name for
    // it, and `_nativeTag` is undefined there. `<DropTarget>` learned the same.
    const tag = (ref.current as unknown as {__nativeTag?: number} | null)
      ?.__nativeTag;
    if (tag == null || windows == null) {
      return;
    }
    windows.setHandledKeys(tag, keys);
    return () => windows.clearHandledKeys(tag);
    // `keys` by identity: an app that rebuilds its list every render will
    // re-register every render, which is correct and cheap. One that memoises
    // will not.
  }, [keys]);

  useEffect(() => {
    const subscription = DeviceEventEmitter.addListener(
      KEY_EVENT,
      (event: PressedKey & {tag?: number}) => {
        const tag = (ref.current as unknown as {__nativeTag?: number} | null)
          ?.__nativeTag;
        // By tag, because every `<KeyHandler>` in the app hears every event: the
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
  }, []);

  return (
    <View ref={ref} focusable={focusOnMount} {...viewProps}>
      {children}
    </View>
  );
}
