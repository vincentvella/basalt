/**
 * `<Button>`, for a desktop React Native platform.
 *
 * Not a shim, and the failure is not a missing branch this time but a missing
 * *key*. React Native's own `Button.js` builds its look out of four
 * `Platform.select({ios, android})` tables, and a platform that is neither gets
 * `undefined` from all four:
 *
 *     button:         undefined   no background, no radius
 *     text:           {textAlign: 'center', margin: 8}   no colour, no size
 *     buttonDisabled: undefined   a disabled button looks enabled
 *     textDisabled:   undefined
 *
 * So `<Button title="Press me" />` renders as black centred text on nothing. It
 * was measured that way on this platform on 2026-10-10, in a picture rather
 * than an argument: see `BASALT_SNAPSHOT` in docs/TESTING.md.
 *
 * **Android's look, deliberately.** The two tables are a filled blue rectangle
 * with white text, and a borderless blue-text button. Android's is the one that
 * stays coherent here, because upstream's `color` prop already behaves the
 * Android way on anything that is not iOS: it sets the *background*. Taking
 * iOS's text styling with that would be black text on a coloured fill, which is
 * exactly the picture the broken version produced.
 *
 * Everything else follows React Native's own file line for line, including
 * which touchable: `Button.js` picks `TouchableNativeFeedback` on Android and
 * `TouchableOpacity` on everything else, and the second is ordinary JavaScript
 * that works here.
 *
 * Two deliberate differences, both from the same rule -- this platform is not
 * Android, so the Android-only behaviour does not come with the Android look:
 *
 *   - the title is not uppercased. Upstream uppercases it under
 *     `Platform.OS === 'android'`, which is a Material convention rather than
 *     part of the styling.
 *   - `touchSoundDisabled`, `hasTVPreferredFocus` and the four `nextFocus`
 *     props are dropped. They are Android and TV props; no desktop has a
 *     touch sound or a focus engine to hand them to, and passing them on would
 *     be a promise nothing keeps.
 *
 * @format
 */

import StyleSheet from 'basalt-core/upstream/Libraries/StyleSheet/StyleSheet';
import Text from 'basalt-core/upstream/Libraries/Text/Text';
import TouchableOpacity from 'basalt-core/upstream/Libraries/Components/Touchable/TouchableOpacity';
import View from 'basalt-core/upstream/Libraries/Components/View/View';
import * as React from 'react';

type AccessibilityState = {
  busy?: boolean;
  checked?: boolean | 'mixed';
  disabled?: boolean;
  expanded?: boolean;
  selected?: boolean;
};

export type ButtonProps = {
  /** The label. Not uppercased here; see the header. */
  title: string;
  onPress?: (event?: unknown) => unknown;
  /** The button's background, as on Android. Upstream tints the text instead on iOS. */
  color?: string | null;
  disabled?: boolean;
  accessible?: boolean;
  accessibilityLabel?: string;
  accessibilityHint?: string;
  accessibilityLanguage?: string;
  accessibilityActions?: ReadonlyArray<{name: string; label?: string}>;
  onAccessibilityAction?: (event: unknown) => void;
  accessibilityState?: AccessibilityState;
  importantForAccessibility?: 'auto' | 'yes' | 'no' | 'no-hide-descendants';
  testID?: string;
  'aria-busy'?: boolean;
  'aria-checked'?: boolean | 'mixed';
  'aria-disabled'?: boolean;
  'aria-expanded'?: boolean;
  'aria-label'?: string;
  'aria-selected'?: boolean;
};

function Button(props: ButtonProps) {
  const {
    accessibilityActions,
    accessibilityHint,
    accessibilityLanguage,
    accessibilityLabel,
    accessibilityState,
    accessible,
    color,
    importantForAccessibility,
    onAccessibilityAction,
    onPress,
    testID,
    title,
  } = props;

  // The aria-* names win over the accessibility* ones, which is the precedence
  // React Native's own file applies.
  const state: AccessibilityState = {
    busy: props['aria-busy'] ?? accessibilityState?.busy,
    checked: props['aria-checked'] ?? accessibilityState?.checked,
    disabled: props['aria-disabled'] ?? accessibilityState?.disabled,
    expanded: props['aria-expanded'] ?? accessibilityState?.expanded,
    selected: props['aria-selected'] ?? accessibilityState?.selected,
  };
  const disabled = props.disabled != null ? props.disabled : state.disabled;

  const buttonStyles = [styles.button];
  const textStyles = [styles.text];
  if (color) {
    buttonStyles.push({backgroundColor: color});
  }
  if (disabled === true) {
    buttonStyles.push(styles.buttonDisabled);
    textStyles.push(styles.textDisabled);
  }

  if (typeof title !== 'string') {
    throw new Error('The title prop of a Button must be a string');
  }

  // A button whose own text is focusable would be two stops in the reading
  // order for one control, which is the same correction upstream makes.
  const importance =
    importantForAccessibility === 'no'
      ? 'no-hide-descendants'
      : importantForAccessibility;

  return (
    <TouchableOpacity
      accessible={accessible}
      accessibilityActions={accessibilityActions}
      onAccessibilityAction={onAccessibilityAction}
      accessibilityLabel={props['aria-label'] || accessibilityLabel}
      accessibilityHint={accessibilityHint}
      accessibilityLanguage={accessibilityLanguage}
      accessibilityRole="button"
      accessibilityState={
        disabled !== state.disabled ? {...state, disabled} : state
      }
      importantForAccessibility={importance}
      testID={testID}
      disabled={disabled}
      onPress={onPress}>
      <View style={buttonStyles}>
        <Text style={textStyles} disabled={disabled}>
          {title}
        </Text>
      </View>
    </TouchableOpacity>
  );
}

// Android's four tables, which are the ones that exist. The colours are
// React Native's own, copied rather than chosen: Material blue for the fill,
// and the greys it disables to.
const styles = StyleSheet.create({
  button: {
    elevation: 4,
    backgroundColor: '#2196F3',
    borderRadius: 2,
  },
  text: {
    textAlign: 'center',
    margin: 8,
    color: 'white',
    fontWeight: '500',
  },
  buttonDisabled: {
    elevation: 0,
    backgroundColor: '#dfdfdf',
  },
  textDisabled: {
    color: '#a1a1a1',
  },
});

Button.displayName = 'Button';

export default Button;
