/**
 * `ActionSheetIOS`, both halves of it.
 *
 * The module was null until 2026-10-10, so the first call died on React
 * Native's own `invariant(RCTActionSheetManager, "ActionSheetManager doesn't
 * exist")` -- no sheet, no choice, and an exception from inside a library the
 * app did not write. There is no `Platform.OS` branch in `ActionSheetIOS.js`,
 * which is why this one needed a module and no JavaScript override.
 *
 * Three things are exercised and each would fail differently:
 *
 *   the sheet    `showActionSheetWithOptions` becomes a popup menu, and the
 *                index it reports is an index into the array this app passed --
 *                not into the menu, which also holds the title and a separator.
 *                That arithmetic is the quiet bug: every index is a valid index.
 *   the anchor   `anchor` is a react tag, which the module resolves through the
 *                shadow tree, so the menu opens under the control rather than at
 *                the pointer. The host logs where it put it.
 *   the share    `showShareActionSheetWithOptions` is iOS's *other* share API,
 *                a callback pair rather than `Share.share()`'s promise, and it
 *                ends up at the same picker.
 *
 * `BASALT_TEST_MENU` answers the menu and `BASALT_TEST_DIALOG` the picker; see
 * native/core/TestDialog.h for why an automated run cannot be shown either.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {ActionSheetIOS, AppRegistry, Platform, StyleSheet, View} from 'react-native';
import {messageOf} from './errors';

console.log(`Platform.OS is ${Platform.OS}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a'},
  // Absolute, so the anchor's position is this file's decision rather than the
  // window's: the scenario asserts the point the module resolved.
  anchor: {position: 'absolute', left: 40, top: 60, width: 100, height: 30,
           backgroundColor: '#56c98a'},
});

// The new renderer's name for a view's tag, with two underscores;
// `_nativeTag` is undefined there. See basalt-core/src/useHandledKeys.ts, which
// learned the same thing.
type MaybeTagged = {__nativeTag?: number} | null;

const OPTIONS = ['Save', 'Discard', 'Cancel'];

function App() {
  const anchor = React.useRef<React.ComponentRef<typeof View> | null>(null);
  const asked = React.useRef(false);

  // `onLayout` rather than an effect: the anchor's tag is only worth passing
  // once the view has a frame, and a sheet shown before layout would be anchored
  // to a box with no size.
  const ask = React.useCallback(() => {
    if (asked.current) {
      return;
    }
    asked.current = true;

    const tag = (anchor.current as MaybeTagged)?.__nativeTag;
    console.log(`action sheet anchor tag: ${tag == null ? 'none' : 'found'}`);

    try {
      ActionSheetIOS.showActionSheetWithOptions(
        {
          title: 'Pick one',
          options: OPTIONS,
          cancelButtonIndex: 2,
          // `Discard`, which a person cannot click and a script cannot answer.
          disabledButtonIndices: [1],
          anchor: tag,
        },
        index => {
          console.log(`action sheet chose: ${index} (${OPTIONS[index]})`);
          share();
        },
      );
    } catch (error) {
      console.log(`action sheet threw: ${messageOf(error)}`);
    }
  }, []);

  // After the sheet, so that one run exercises both and the order in the log is
  // fixed. A dismissal with a cancel index still reports one, so this is reached
  // either way.
  const share = () => {
    try {
      ActionSheetIOS.showShareActionSheetWithOptions(
        {message: 'basalt action sheet', url: 'https://basaltjs.dev', subject: 'A link'},
        error => {
          console.log(`share sheet failed: ${messageOf(error)}`);
        },
        (completed: boolean, method: string | null) => {
          console.log(`share sheet: completed=${String(completed)} method=${String(method)}`);
        },
      );
    } catch (error) {
      console.log(`share sheet threw: ${messageOf(error)}`);
    }
  };

  return (
    <View style={styles.page}>
      <View ref={anchor} style={styles.anchor} onLayout={ask} />
    </View>
  );
}

AppRegistry.registerComponent('BasaltActionSheet', () => App);
