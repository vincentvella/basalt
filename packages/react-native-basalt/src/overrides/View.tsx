/**
 * React Native's `View`, plus the keyboard props react-native-macos has.
 *
 * `keyDownEvents`, `onKeyDown` and `focusable` are how an app written against
 * react-native-macos declares a keyboard shortcut, and an app that has them is
 * an app this platform was supposed to be able to replace. Without them the
 * props are dropped in JavaScript, silently -- no error, no warning, and every
 * binding in the app simply never fires, which is exactly how it was found.
 *
 * `<KeyHandler>` already did this under names of this project's own. Those names
 * are better -- a component that takes `keys` cannot be spread onto a view that
 * does nothing with it -- and they are no use to an app that is not being
 * rewritten. Both work now, through one implementation: see useHandledKeys.ts.
 *
 * **Two components, not one branch.** A view without `keyDownEvents` renders
 * React Native's own with no hooks added, because hooks cannot be conditional
 * and three of them on every view in an app is not a cost to impose for a prop
 * almost nothing uses. The consequence is that a view which *gains* the prop
 * changes component type and remounts, which is the right trade for a prop that
 * is either on a view for its lifetime or not on it at all.
 *
 * The callback is handed `{nativeEvent}`, which is react-native-macos's shape
 * and what an app already written for it expects -- not the bare object
 * `<KeyHandler>` passes. Different names, different shapes; both are contracts
 * and neither gets to change the other.
 *
 * @format
 */

import View from 'react-native-basalt/upstream/Libraries/Components/View/View';
import * as React from 'react';

import type {HandledKey, PressedKey} from '../useHandledKeys';
import {useHandledKeys} from '../useHandledKeys';

type ViewRef = React.ComponentRef<typeof View>;

type KeyProps = {
  keyDownEvents?: ReadonlyArray<HandledKey>;
  onKeyDown?: (event: {nativeEvent: PressedKey}) => void;
};

// The props React Native's own View takes, which this neither narrows nor
// documents: it is a pass-through and the upstream types are the truth.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
type Props = any;

/** The one that listens. Only mounted for a view that asked. */
function KeyboardView(props: Props): React.ReactElement {
  const {keyDownEvents, onKeyDown, ref, ...rest} = props as Props & KeyProps;
  const own = React.useRef<ViewRef | null>(null);

  // The caller's ref and ours, both. A view that declares shortcuts is usually
  // also one an app holds a reference to -- kino's root view is both -- so
  // taking the ref for ourselves would break the app in a second way while
  // fixing the first.
  const setRef = React.useCallback(
    (node: ViewRef | null) => {
      own.current = node;
      if (typeof ref === 'function') {
        ref(node);
      } else if (ref != null) {
        (ref as {current: ViewRef | null}).current = node;
      }
    },
    [ref],
  );

  const onPress = React.useCallback(
    (pressed: PressedKey) => onKeyDown?.({nativeEvent: pressed}),
    [onKeyDown],
  );
  useHandledKeys(own, keyDownEvents, onPress);

  return <View {...rest} ref={setRef} />;
}

function BasaltView(props: Props): React.ReactElement {
  // Read, not destructured, so that every other prop reaches React Native's
  // View exactly as it was given.
  return (props as KeyProps).keyDownEvents == null ? (
    <View {...props} />
  ) : (
    <KeyboardView {...props} />
  );
}

BasaltView.displayName = 'View';

export default BasaltView;
