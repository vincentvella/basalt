/**
 * A native context menu.
 *
 *   const menu = useContextMenu();
 *
 *   <Pressable
 *     onLongPress={event =>
 *       menu.show(
 *         [
 *           {label: 'Copy', onSelect: copy},
 *           {separator: true},
 *           {label: 'Delete', onSelect: remove, shortcut: 'Del'},
 *           {label: 'Paste', enabled: false},
 *         ],
 *         event.nativeEvent,
 *       )
 *     }
 *   />
 *
 * React Native has no API for this because a phone has no pointer to click the
 * other button of. On a desktop it is the second thing an app wants after a
 * menu bar -- and on Linux it is the *first*, because GNOME has no menu bar and
 * `Menu.isSupported` is false there.
 *
 * ## One level, on purpose
 *
 * No submenus and no roles. A popup menu on all three desktops is a list, and
 * the nesting an application menu has is what a menu *bar* is for -- so a
 * `<Menu>` and this are different shapes rather than the same shape twice.
 *
 * ## Where it appears
 *
 * `show(items, where)` takes anything with an `x` and a `y` in the window's
 * coordinates, which is what a press event's `nativeEvent` already is. Pass
 * nothing and the menu opens wherever the pointer is, which is what a menu
 * opened from the keyboard wants.
 *
 * ## What it answers
 *
 * `show()` resolves with the index chosen, or null if the menu was dismissed --
 * and calls that item's `onSelect` first, which is what most callers want:
 *
 *   const chosen = await menu.show(items);
 *
 * A promise rather than the device event `<Menu>` uses, because the two are
 * different situations. A menu bar is installed once and its items are chosen
 * at times nothing is waiting for; a context menu is opened by an app that is,
 * right then, asking a question.
 *
 * ## A right-click
 *
 *   <View
 *     onPointerDown={e => e.nativeEvent.button === 2 && menu.show(items, e.nativeEvent)}
 *   />
 *
 * A secondary click arrives as `onPointerDown` with W3C's `button === 2`, and
 * does *not* fire `onPress` -- on any of the three. So the same view can be a
 * button and have a context menu, which is what a desktop expects.
 *
 * `onLongPress` still works and is what a touch-first app should use. See
 * native/core/PointerButtons.h for what each toolkit had to be told.
 *
 * @format
 */

import type {TurboModule} from 'react-native';
import {TurboModuleRegistry} from 'react-native';

/**
 * One item in a context menu: the four fields `entriesFrom` in
 * `native/core/MenuModule.cpp` reads into a `MenuEntry`, plus the handler this
 * file calls.
 *
 * A popup menu is a flat list on all three desktops, so there is no `submenu`
 * and no `role` here -- nesting and the platform roles are what a menu *bar*
 * is for, and `<Menu.Item>` is where they live.
 *
 * `onSelect` is the only field the native side does not see: the promise
 * carries the index back, and this calls the handler before settling it.
 */
export type ContextMenuItem = {
  label?: string;
  /**
   * Drawn beside the label -- "Cmd+C", "Del". Decoration only: nothing here
   * binds a key. Note the menu *bar* spells the same idea `accelerator`,
   * because there it really does bind one.
   */
  shortcut?: string;
  enabled?: boolean;
  separator?: boolean;
  /**
   * One of the platform roles, in Electron's spelling: `about`, `quit`, `undo`,
   * `redo`, `cut`, `copy`, `paste`, `delete`, `selectAll`, `minimize`, `zoom`,
   * `close`, `togglefullscreen`. The platform performs it -- the Copy that
   * actually copies the field the person was in -- and `onSelect` still runs, so
   * an app can both get the behaviour and hear about it.
   *
   * **A role this desktop cannot perform is left out of the menu**, rather than
   * shown doing nothing. It keeps its index though: what comes back is an index
   * into the list you passed, so adding a role that one platform lacks does not
   * renumber the items after it.
   *
   * The three do not agree. macOS has all thirteen; Linux and Windows have
   * twelve, both missing `about`, neither having a platform about panel to ask
   * for -- that window is an app's own to show, and an ordinary item with an
   * `onSelect` is how to open it.
   */
  role?: string;
  /**
   * A tick or a radio mark beside the label.
   *
   * `checked` on its own means a checkbox, so the common case is one word. The
   * kind is a separate field because it has to be: a GMenu item carries no
   * checked attribute, and GTK picks a tick or a circle from the shape of the
   * action behind it, which a boolean cannot express.
   *
   * `'radio'` is a drawing hint, not a capability. Windows draws a bullet and
   * Linux a circle; macOS draws a tick, `NSMenu` having no radio item and a tick
   * being what Apple's guidance prescribes for a chosen member of a group.
   *
   * A group is a run of adjacent `'radio'` items, ended by a separator or by any
   * other kind of item. There is no group name to pass. Exclusivity is yours:
   * the menu is built fresh each time it opens, so it draws the marks you give
   * it and keeping one member checked is the caller's.
   */
  type?: 'checkbox' | 'radio';
  checked?: boolean;
  /**
   * Nested items. The parent is not choosable: it opens its children.
   *
   * Both a `submenu` and a `role` on one item is a mistake, and the submenu
   * wins. A parent whose children are all roles this desktop cannot perform is
   * left out with them, an empty submenu being a dead end rather than an item.
   */
  submenu?: ReadonlyArray<ContextMenuItem>;
  onSelect?: () => void;
};

/**
 * Where to open it. A press event carries `pageX`/`pageY`; a caller with a
 * point in hand carries `x`/`y`. Either, or neither for wherever the pointer
 * is.
 */
export type ContextMenuPoint = {
  x?: number;
  y?: number;
  pageX?: number;
  pageY?: number;
};

type NativeMenuModule = TurboModule & {
  showContextMenu(
    items: ReadonlyArray<ContextMenuItem>,
    x: number,
    y: number,
  ): Promise<number | null>;
};

const NativeMenu = TurboModuleRegistry.get<NativeMenuModule>('BasaltMenu');

/**
 * Whether this platform can show one.
 *
 * True on all three, unlike `Menu.isSupported`: a menu bar is what GNOME does
 * not have, and a popup menu is something every desktop has always had.
 */
export const isSupported = NativeMenu?.showContextMenu != null;

async function show(
  items: ReadonlyArray<ContextMenuItem>,
  where?: ContextMenuPoint | null,
): Promise<number | null> {
  if (!isSupported || !Array.isArray(items) || items.length === 0) {
    return null;
  }
  // Negative is "wherever the pointer is"; see the header. `pageX`/`pageY` as
  // well as `x`/`y`, because a press event carries the first pair and a caller
  // with a point in hand carries the second.
  const x = where?.x ?? where?.pageX ?? -1;
  const y = where?.y ?? where?.pageY ?? -1;

  // `isSupported` is what proves this is non-null, and the compiler cannot see
  // through that -- it is computed at module scope from the same optional call.
  const index = await NativeMenu!.showContextMenu(items, x, y);
  if (index == null) {
    return null;
  }
  // Called before the promise settles, so that an app which only wants
  // `onSelect` never has to await anything.
  itemAt(items, index)?.onSelect?.();
  return index;
}

/**
 * The item an index names, which is its position in a pre-order walk of the
 * whole menu: an item, then its children, then the next item, counting
 * separators and parents.
 *
 * For a flat list that is the position in the array, which is what this answered
 * when a popup could only be flat, so an index means what it has always meant.
 * The native side numbers its items the same way; see `walkMenuEntries` in
 * core/PlatformServices.h, which is the other half of this contract.
 *
 * An item the platform left out still counts, which is what lets both sides
 * agree without this knowing which roles the platform has: the native side
 * numbers every entry it was given and draws only the ones it can. See
 * `menuEntryShown` beside it.
 */
function itemAt(
  items: ReadonlyArray<ContextMenuItem>,
  index: number,
): ContextMenuItem | undefined {
  let next = 0;
  const walk = (
    level: ReadonlyArray<ContextMenuItem>,
  ): ContextMenuItem | undefined => {
    for (const item of level) {
      if (next++ === index) {
        return item;
      }
      if (item.submenu != null && item.submenu.length > 0) {
        const found = walk(item.submenu);
        if (found != null) {
          return found;
        }
      }
    }
    return undefined;
  };
  return walk(items);
}

/**
 * The imperative half, usable outside a component -- the same arrangement
 * `useDialog()` and `windowControl` have.
 */
export const contextMenu = Object.freeze({show, isSupported});

export type ContextMenu = typeof contextMenu;

export function useContextMenu(): ContextMenu {
  // Frozen and module-level, so this is a stable identity rather than a new
  // object every render: an app putting `menu` in a dependency array should not
  // get a new one each time.
  return contextMenu;
}
