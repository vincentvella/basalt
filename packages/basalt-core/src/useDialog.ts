/**
 * The native file dialogs.
 *
 *   const dialog = useDialog();
 *
 *   const {canceled, paths} = await dialog.openFile({
 *     title: 'Choose an image',
 *     multiple: true,
 *     filters: [{name: 'Images', extensions: ['png', 'jpg', 'jpeg']}],
 *   });
 *
 *   const {canceled, paths: [path]} = await dialog.saveFile({
 *     defaultPath: 'notes.md',
 *   });
 *
 *   const {canceled, paths: [folder]} = await dialog.openFolder();
 *
 * React Native has no API for any of this, because a phone has no file dialog,
 * so there is nothing here to be compatible with. The semantics follow
 * Electron's -- the vocabulary a desktop developer already has -- and the shape
 * is React Native's.
 *
 * ## Why every one of them answers `{canceled, paths}`
 *
 * Because "the person pressed Cancel" and "the person chose nothing" are
 * different answers, and every API that collapses them is one somebody has to
 * work around. `paths` is a list even for a save and a folder, where it always
 * has one entry, so that an app moving between the three is not also moving
 * between result types:
 *
 *   const {canceled, paths: [path]} = await dialog.saveFile();
 *   if (canceled) return;
 *
 * ## Why a hook rather than a module
 *
 * `useDialog()` returns the same object every time and does not subscribe to
 * anything, so as a hook it buys exactly one thing: it is the shape the rest of
 * this package uses, and a component that calls it reads like a component. An
 * app that wants it outside a component can import `dialog` instead -- it is
 * the same object, and the hook returns it.
 *
 * On a host with no dialog module every call resolves `{canceled: true, paths:
 * []}` rather than throwing, so the same code runs on a platform that has not
 * implemented one yet.
 *
 * @format
 */

import type {TurboModule} from 'react-native';
import {TurboModuleRegistry} from 'react-native';

/**
 * A file type a dialog offers to filter by. Extensions are bare -- 'png', not
 * '.png' and not 'image/png' -- which is what all three desktops translate
 * from.
 */
export type DialogFilter = {
  name: string;
  extensions: ReadonlyArray<string>;
};

export type DialogOptions = {
  title?: string;
  defaultPath?: string;
  confirmLabel?: string;
  /** Open only; a save and a folder choose one. */
  multiple?: boolean;
  filters?: ReadonlyArray<DialogFilter>;
};

/**
 * Always both, and always a list. "The person pressed Cancel" and "the person
 * chose nothing" are different answers; `paths` is a list even where it can
 * only have one entry, so an app moving between the three calls is not also
 * moving between result types.
 */
export type DialogResult = {
  canceled: boolean;
  paths: ReadonlyArray<string>;
};

type NativeDialogModule = TurboModule & {
  openFile(options: DialogOptions): Promise<Partial<DialogResult> | null>;
  saveFile(options: DialogOptions): Promise<Partial<DialogResult> | null>;
  openFolder(options: DialogOptions): Promise<Partial<DialogResult> | null>;
};

const NativeDialog = TurboModuleRegistry.get<NativeDialogModule>('BasaltDialog');

const CANCELED: DialogResult = Object.freeze({
  canceled: true,
  paths: Object.freeze([]) as ReadonlyArray<string>,
});

/**
 * Normalises whatever the native side answered.
 *
 * The host is this project's own and answers the right shape, so this is not
 * defensive about it: what it is for is the host that has no module at all,
 * where there is no answer to normalise.
 */
function answer(result: Partial<DialogResult> | null | undefined): DialogResult {
  if (result == null || !Array.isArray(result.paths)) {
    return CANCELED;
  }
  return {canceled: result.canceled === true, paths: result.paths};
}

export const dialog = Object.freeze({
  /**
   * One or more existing files.
   *
   * `{title, defaultPath, confirmLabel, multiple, filters}`, all optional.
   * `filters` is `[{name, extensions}]` with the extensions bare -- 'png', not
   * '.png' and not 'image/png' -- which is what all three desktops translate
   * from.
   */
  async openFile(options?: DialogOptions | null): Promise<DialogResult> {
    if (NativeDialog == null) {
      return CANCELED;
    }
    return answer(await NativeDialog.openFile(options ?? {}));
  },

  /**
   * A path to write to, which may not exist yet. `defaultPath` is the
   * suggested name, optionally with a directory in front of it.
   *
   * Whether an existing file may be overwritten is the platform's question and
   * it asks for itself; by the time this resolves, the answer was yes.
   */
  async saveFile(options?: DialogOptions | null): Promise<DialogResult> {
    if (NativeDialog == null) {
      return CANCELED;
    }
    return answer(await NativeDialog.saveFile(options ?? {}));
  },

  /** A directory. `filters` means nothing here and is ignored. */
  async openFolder(options?: DialogOptions | null): Promise<DialogResult> {
    if (NativeDialog == null) {
      return CANCELED;
    }
    return answer(await NativeDialog.openFolder(options ?? {}));
  },
});

export type Dialog = typeof dialog;

export function useDialog(): Dialog {
  // The same object every render. Nothing here depends on a render, so a new
  // one would only make every effect that lists it a dependency run again.
  return dialog;
}
