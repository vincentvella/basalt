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
import React, {useRef} from 'react';
import {View, type ViewProps} from 'react-native';

// The registering and the listening are in a hook, because the `View` override
// does the same thing for `keyDownEvents` and two copies would drift.
import type {HandledKey, PressedKey} from './useHandledKeys';
import {useHandledKeys} from './useHandledKeys';

export type {HandledKey, PressedKey} from './useHandledKeys';

// `onKeyDown` is omitted rather than intersected: a view's own `onKeyDown` takes
// a key event, this one takes the `PressedKey` that `useHandledKeys` matched, and
// an intersection of the two is a prop no handler can satisfy. Omitting is also
// what the component does -- `onKeyDown` is destructured out below and never
// reaches the inner `<View>`.
export type KeyHandlerProps = Omit<ViewProps, 'onKeyDown'> & {
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
  useHandledKeys(ref, keys, onKeyDown);
  return (
    <View ref={ref} focusable={focusOnMount} {...viewProps}>
      {children}
    </View>
  );
}
