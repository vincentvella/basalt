/**
 * `<TextInput>`, for every desktop platform here.
 *
 * React Native's own `TextInput.js` cannot be used here. Its body is
 *
 *     if (Platform.OS === 'android') { ... }
 *     else if (Platform.OS === 'ios') { ... }
 *
 * and on any other platform neither branch runs, so the component it goes on to
 * render is undefined and React reports an invalid element type. There is no
 * third branch to add without forking fifteen hundred lines of iOS and Android
 * behaviour that would then drift from upstream in silence.
 *
 * So this is a smaller implementation, and honestly a subset. It renders the
 * same native component the iOS path renders, `RCTSinglelineTextInputView`,
 * which ReactCommon rewrites to the component named `TextInput`, whose shadow
 * node is React Native's own iOS one and whose measurement runs through
 * whichever text layout manager the platform installs -- Pango on Linux, Core
 * Text on macOS and DirectWrite on Windows, all three of which mount it. What
 * it does not carry is the years of platform-specific behaviour in the real
 * file: no `InputAccessoryView`, no shared `TextInputState` focus registry, no
 * autofill, no `rejectResponderTermination`, no multiline.
 *
 * `selection` and `onSelectionChange` do work, and need nothing here: both
 * reach the native component through `...rest`, and what was missing was the
 * native half on either side of them.
 *
 * `docs/BACKLOG.md` tracks what is missing. The seam is here when it is time.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  useCallback,
  useEffect,
  useImperativeHandle,
  useMemo,
  useRef,
  useState,
} from 'react';
import RCTSinglelineTextInputNativeComponent, {
  Commands,
} from 'basalt-core/upstream/Libraries/Components/TextInput/RCTSingelineTextInputNativeComponent';
import TextInputState from 'basalt-core/upstream/Libraries/Components/TextInput/TextInputState';
import StyleSheet from 'basalt-core/upstream/Libraries/StyleSheet/StyleSheet';

/**
 * React Native's text fields are controlled: JavaScript owns the value and
 * every keystroke round-trips through a re-render. `mostRecentEventCount` is
 * how the native side tells a stale prop from a current one. Without it a fast
 * typist outruns the render loop and watches characters reorder themselves.
 */
/**
 * The props this file reads. Everything else is forwarded untouched, which is
 * what the index signature says -- React Native's `TextInputProps` is large,
 * exported as no usable type, and this is a rewrite of its component rather
 * than a re-declaration of its surface.
 */
export type BasaltTextInputProps = {
  value?: string;
  defaultValue?: string;
  onChange?: (event: TextInputChangeEvent) => void;
  onChangeText?: (text: string) => void;
  onFocus?: (event: unknown) => void;
  onBlur?: (event: unknown) => void;
  editable?: boolean;
  style?: unknown;
  [key: string]: unknown;
};

/** What the native component reports on every edit. */
type TextInputChangeEvent = {
  nativeEvent: {text: string; eventCount: number; target?: number};
};

/** The handle a caller gets, which is this and not the host instance. */
export type BasaltTextInputHandle = {
  focus: () => void;
  blur: () => void;
  clear: () => void;
  setSelection: (start: number, end: number) => void;
  isFocused: () => boolean;
  /**
   * React Native's own field instances carry this and upstream's guards read
   * it. Optional, because the handle this file builds does not.
   */
  currentProps?: {editable?: boolean};
};

function TextInput(
  {
    value,
    defaultValue,
    onChange,
    onChangeText,
    onFocus,
    onBlur,
    editable,
    style,
    ...rest
  }: BasaltTextInputProps,
  forwardedRef: React.Ref<BasaltTextInputHandle>,
): React.ReactElement {
  const inputRef = useRef<React.ElementRef<typeof RCTSinglelineTextInputNativeComponent> | null>(
    null,
  );
  const [mostRecentEventCount, setMostRecentEventCount] = useState(0);
  const focused = useRef(false);


  // Built as one object rather than inline, because the registry has to hold
  // the *same* thing callers do. `useImperativeHandle` means the ref a caller
  // gets is this handle and not the host instance, so registering the host
  // instance would make `isTextInput(ref)` answer false about a field that is
  // very much one -- which is exactly what it did first time round.
  const handle = useMemo(
    () => ({
      focus() {
        if (inputRef.current != null) {
          Commands.focus(inputRef.current);
        }
      },
      blur() {
        if (inputRef.current != null) {
          Commands.blur(inputRef.current);
        }
      },
      clear() {
        if (inputRef.current != null) {
          Commands.setTextAndSelection(
            inputRef.current,
            mostRecentEventCount,
            '',
            0,
            0,
          );
        }
        onChangeText?.('');
      },
      setSelection(start: number, end: number) {
        if (inputRef.current != null) {
          Commands.setTextAndSelection(
            inputRef.current,
            mostRecentEventCount,
            null,
            start,
            end,
          );
        }
      },
      isFocused() {
        return focused.current;
      },
    }),
    [mostRecentEventCount, onChangeText],
  );

  useImperativeHandle(forwardedRef, () => handle, [handle]);

  // The shared registry, which is what makes `TextInput.State` and
  // `dismissKeyboard`-shaped code work: a field has to be in `inputs` before
  // anything can ask whether it is one, and has to leave when it unmounts or
  // the set grows for the life of the app.
  useEffect(() => {
    TextInputState.registerInput(handle);
    return () => {
      TextInputState.unregisterInput(handle);
      // A field that unmounts while focused would otherwise leave the registry
      // pointing at something React has thrown away, and
      // `currentlyFocusedInput()` would answer with it.
      if (TextInputState.currentlyFocusedInput() === handle) {
        TextInputState.blurInput(handle);
      }
    };
  }, [handle]);

  const handleChange = useCallback(
    (event: TextInputChangeEvent) => {
      const {text, eventCount} = event.nativeEvent;
      setMostRecentEventCount(eventCount);
      onChange?.(event);
      onChangeText?.(text);
    },
    [onChange, onChangeText],
  );

  const handleFocus = useCallback(
    (event: TextInputChangeEvent) => {
      focused.current = true;
      // The platform is the authority on what has focus, so the registry is
      // told here rather than by whoever called focus() -- a click into a field
      // never goes through that path at all.
      TextInputState.focusInput(handle);
      onFocus?.(event);
    },
    [handle, onFocus],
  );

  const handleBlur = useCallback(
    (event: TextInputChangeEvent) => {
      focused.current = false;
      TextInputState.blurInput(handle);
      onBlur?.(event);
    },
    [handle, onBlur],
  );

  return (
    <RCTSinglelineTextInputNativeComponent
      {...rest}
      ref={inputRef}
      style={[styles.input, style]}
      // `text` rather than `value`: the iOS view config calls it that, and this
      // renders the iOS component. An uncontrolled field is given its default
      // once and then left alone, which is what passing undefined does.
      text={value != null ? value : defaultValue}
      editable={editable !== false}
      mostRecentEventCount={mostRecentEventCount}
      onChange={handleChange}
      onFocus={handleFocus}
      onBlur={handleBlur}
    />
  );
}

const styles = StyleSheet.create({
  input: {
    // Yoga decides the size, and a field measured from empty text is a couple
    // of pixels tall -- invisible, unclickable, and indistinguishable from the
    // component being broken. Any explicit height in `style` overrides this.
    minHeight: 36,
  },
});

const ForwardedTextInput = React.forwardRef(TextInput);

/**
 * `TextInput.State`, which is React Native's own registry with two methods
 * replaced.
 *
 * `focusTextInput` and `blurTextInput` upstream are
 *
 *     if (Platform.OS === 'ios') { iOSTextInputCommands.focus(ref); }
 *     else if (Platform.OS === 'android') { ... }
 *
 * -- the same missing third branch that makes TextInput.js itself unusable
 * here. On these platforms they would update the registry and then focus
 * nothing, which is worse than not existing: `currentlyFocusedInput()` would
 * name a field that does not have focus.
 *
 * So the rest of the module is taken as it is, and these two dispatch the
 * commands the component already uses. Wrapped rather than forked, because the
 * other nine functions are the registry itself and have no platform in them.
 */
const State = {
  ...TextInputState,

  focusTextInput(textField: BasaltTextInputHandle | null) {
    if (textField == null) {
      return;
    }
    // Upstream's guards, which are not incidental: focusing what is already
    // focused would emit a second onFocus, and a disabled field must refuse.
    if (
      TextInputState.currentlyFocusedInput() === textField ||
      textField.currentProps?.editable === false
    ) {
      return;
    }
    TextInputState.focusInput(textField);
    // The handle's own method, which dispatches the command against the host
    // instance it closed over. `Commands.focus` cannot be called with the
    // handle, and the handle is what the registry holds.
    textField.focus?.();
  },

  blurTextInput(textField: BasaltTextInputHandle | null) {
    if (textField == null) {
      return;
    }
    if (TextInputState.currentlyFocusedInput() !== textField) {
      return;
    }
    TextInputState.blurInput(textField);
    textField.blur?.();
  },
};

// The statics React Native's TextInput carries. Assigned through an indexed
// cast because a forwardRef component's type has no room for them.
(ForwardedTextInput as unknown as Record<string, unknown>).State = State;

export default ForwardedTextInput;
