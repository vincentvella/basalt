/**
 * A React Native app about the application menu.
 *
 * Two things worth watching, and only one of them is on screen.
 *
 * The visible one is the menu itself: a File menu with the app's own commands,
 * and an Edit menu built entirely out of roles.
 *
 * The invisible one is why the Edit menu is there. On macOS AppKit hands every
 * key equivalent to the main menu before the responder chain sees it, so
 * `role="copy"` is what makes Cmd-C reach the <TextInput> below -- and an app
 * that renders no <Menu> at all still gets an Edit menu, because it still has
 * text fields. Type in the field and copy: without the menu, nothing happens.
 *
 * A second app below is about the *other* kind of menu -- the one that pops up
 * where you press. Every desktop has one, including the one with no menu bar,
 * which is what makes it the more portable of the two.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import {
  AppRegistry,
  Platform,
  Pressable,
  StyleSheet,
  Text,
  TextInput,
  View,
} from 'react-native';
import {Menu, useContextMenu} from 'basalt-core';
import type {ContextMenuPoint} from 'basalt-core';

console.log(`Platform.OS is ${Platform.OS}`);
console.log(`menu supported: ${Menu.isSupported}`);

const styles = StyleSheet.create({
  page: {flex: 1, backgroundColor: '#11131a', padding: 24},
  label: {color: '#aab', fontSize: 13, height: 22},
  field: {
    height: 44,
    borderRadius: 8,
    paddingHorizontal: 12,
    backgroundColor: '#1b1f2a',
    color: '#fff',
    marginBottom: 12,
  },
  chosen: {color: '#dde', fontSize: 14, height: 22},
  button: {
    width: 220,
    height: 48,
    borderRadius: 8,
    marginBottom: 12,
    backgroundColor: '#4285f4',
    alignItems: 'center',
    justifyContent: 'center',
  },
  buttonText: {color: '#fff', fontSize: 15},
});

function App() {
  const [chosen, setChosen] = React.useState('nothing yet');

  return (
    <View style={styles.page}>
      <Menu>
        <Menu.Submenu label="File">
          <Menu.Item
            label="New"
            accelerator="CmdOrCtrl+N"
            onClick={() => {
              console.log('menu chose: New');
              setChosen('New');
            }}
          />
          <Menu.Item
            label="Open…"
            accelerator="CmdOrCtrl+O"
            onClick={() => {
              console.log('menu chose: Open');
              setChosen('Open');
            }}
          />
          <Menu.Separator />
          <Menu.Item label="Nothing doing" enabled={false} onClick={() => {}} />
          <Menu.Item role="close" />
        </Menu.Submenu>
        <Menu.Submenu label="Edit">
          <Menu.Item role="undo" />
          <Menu.Item role="redo" />
          <Menu.Separator />
          <Menu.Item role="cut" />
          <Menu.Item role="copy" />
          <Menu.Item role="paste" />
          <Menu.Item role="selectAll" />
        </Menu.Submenu>
      </Menu>

      <Text style={styles.label}>Select this text and copy it</Text>
      <TextInput style={styles.field} defaultValue="Copy me with the Edit menu" />
      <Text style={styles.chosen}>{`chose: ${chosen}`}</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltMenu', () => App);

/**
 * The other kind of menu: the one that pops up where you press.
 *
 * Its own screen rather than another button on the one above, because the two
 * are different shapes for different jobs -- a menu bar nests and carries
 * roles, a popup is a list -- and a screen showing both would suggest they are
 * the same thing spelled twice.
 *
 * Opened two ways, which is the point of the second one. A right-click reaches
 * `onPointerDown` with `button === 2` and does *not* fire `onPress` -- so the
 * same view can be a button and have a context menu, which is what a desktop
 * expects and what every host used to get wrong in its own way. See
 * core/PointerButtons.h.
 */
function Context() {
  const menu = useContextMenu();
  const [chosen, setChosen] = React.useState('nothing yet');

  // Logged as well as rendered, because `onSelect` running is the half most
  // callers use and a rendered string is not visible to a run that does not
  // dump the tree.
  const pick = (label: string) => () => {
    console.log(`context menu selected: ${label}`);
    setChosen(label);
  };

  const open = async (where: ContextMenuPoint) => {
    const index = await menu.show(
      // The indexes are a pre-order walk of the whole thing, counting separators
      // and submenu parents, which is why the numbers in the comments jump the
      // way they do. Anything added here goes after Delete: the suite names
      // index 2 for Rename, and renumbering it would be renaming the test.
      [
        {label: 'Copy', shortcut: 'Cmd+C', onSelect: pick('Copy')}, // 0
        {separator: true}, //                                          1
        {label: 'Rename', onSelect: pick('Rename')}, //                2
        {label: 'Delete', enabled: false, onSelect: pick('Delete')}, // 3
        {separator: true}, //                                          4
        {
          label: 'Share', //                                           5
          submenu: [
            {label: 'Copy link', onSelect: pick('Copy link')}, //      6
            {label: 'Email', onSelect: pick('Email')}, //              7
          ],
        },
        // A role: the platform selects the text, and this still hears about it.
        {label: 'Select All', role: 'selectAll', onSelect: pick('Select All')}, // 8
        // A role only macOS has. Left out of the menu on Linux and Windows and
        // still counted, which is what keeps `Last` at 10 everywhere -- the one
        // thing most likely to be got wrong, so the suite names 10 on purpose.
        {label: 'About', role: 'about', onSelect: pick('About')}, //   9
        {label: 'Last', onSelect: pick('Last')}, //                    10
      ],
      where,
    );
    // The index says a dismissal and a choice are told apart, and `null` for
    // dismissed is the same shape the file dialogs answer a cancel with.
    console.log(`context menu answered: ${index == null ? 'dismissed' : index}`);
  };

  return (
    <View style={styles.page}>
      <Text style={styles.label}>Context menu</Text>
      <Pressable
        style={styles.button}
        // A right-click, which is what a context menu is actually for. The
        // button number is W3C's: 2 is secondary. A secondary click does not
        // fire `onPress` -- on any of the three -- which is the whole reason
        // this can live on the same view as a button.
        onPointerDown={event => {
          if (event.nativeEvent.button !== 2) {
            return;
          }
          console.log('context menu: opening from a right-click');
          open(event.nativeEvent);
        }}
        onPress={event => {
          console.log('context menu: opening');
          open(event.nativeEvent);
        }}>
        <Text style={styles.buttonText}>Open a context menu</Text>
      </Pressable>
      <Text style={styles.chosen}>{`chose: ${chosen}`}</Text>
    </View>
  );
}

AppRegistry.registerComponent('BasaltContextMenu', () => Context);
