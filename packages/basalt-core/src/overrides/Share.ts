/**
 * `Share`, for a platform React Native's own copy does not have a branch for.
 *
 * Not a shim and not a subset: this is a rewrite, for the same reason
 * TextInput.js is one. React Native's `Share.share()` branches on `Platform.OS`
 * being exactly `android` or `ios` and ends with
 *
 *     return Promise.reject(new Error('Unsupported platform'));
 *
 * so on a desktop the native module was never reached, and implementing one
 * changed nothing at all. A third branch upstream would be the tidier fix and
 * is not ours to make; until then the file is replaced, and the replacement is
 * deliberately close to the original so a diff against it stays short.
 *
 * ## Which of the two shapes this follows
 *
 * Android's. React Native has two: a promise-returning `ShareModule.share`, and
 * an iOS callback pair on `ActionSheetManager`. The first is the one whose
 * signature matches what `Share.share()` returns, so the desktop module is
 * shaped like it -- see core/CoreModules.h.
 *
 * It takes one thing more than Android's does: `url`. React Native marks that
 * as iOS-only, and a desktop has no reason to refuse it -- a link with a
 * sentence about it is what most calls to Share are, and dropping either half
 * would be dropping half of what the app asked for.
 *
 * ## What the platforms do with it
 *
 * macOS shows `NSSharingServicePicker`, which is the real thing. Linux and
 * Windows show a small picker built from a clipboard and a mail client, because
 * Linux has no share service at all and Windows' needs WinRT interop that is
 * not written yet. See native/core/ShareFallback.h, which argues for that being
 * the honest answer rather than a rejection.
 *
 * @format
 */

'use strict';

import NativeShareModule from 'basalt-core/upstream/Libraries/Share/NativeShareModule';

const invariant = require('invariant');

/** One or the other must be there, which is what the union says. */
export type ShareContent =
  | {
      title?: string;
      url: string;
      message?: string;
    }
  | {
      title?: string;
      url?: string;
      message: string;
    };

export type ShareOptions = {
  dialogTitle?: string;
  /** iOS's, accepted and ignored, so code written for it runs unchanged. */
  excludedActivityTypes?: Array<string>;
  tintColor?: string;
  subject?: string;
  anchor?: number;
};

export type ShareAction = {
  action: 'sharedAction' | 'dismissedAction';
  /** Null here for the same reason it is null on Android. */
  activityType?: string | null;
};

class Share {
  /**
   * Open a dialog to share text content.
   *
   * Returns a Promise which will be invoked with an object containing `action`.
   * If the user dismissed the dialog, the Promise will still be resolved with
   * action being `Share.dismissedAction` and all the other keys being undefined.
   *
   * **Content:**
   *
   * - `message` - A message to share.
   * - `url` - A URL to share.
   * - `title` - Title of the message, and the subject of an email if the share
   *   goes that way.
   *
   * At least one of `url` or `message` is required.
   *
   * **Options:**
   *
   * - `dialogTitle` - Title of the share dialog, where one is drawn rather than
   *   supplied by the system.
   *
   * `excludedActivityTypes`, `tintColor`, `subject` and `anchor` are iOS's and
   * are accepted and ignored, so that code written for iOS runs unchanged.
   */
  static share(content: ShareContent, options: ShareOptions = {}): Promise<ShareAction> {
    invariant(
      typeof content === 'object' && content !== null,
      'Content to share must be a valid object',
    );
    invariant(
      typeof content.url === 'string' || typeof content.message === 'string',
      'At least one of URL or message is required',
    );
    invariant(
      typeof options === 'object' && options !== null,
      'Options must be a valid object',
    );
    invariant(
      content.title == null || typeof content.title === 'string',
      'Invalid title: title should be a string.',
    );
    invariant(
      NativeShareModule,
      'ShareModule should be registered on this platform.',
    );

    return NativeShareModule.share(
      {
        title: content.title,
        message:
          typeof content.message === 'string' ? content.message : undefined,
        url: typeof content.url === 'string' ? content.url : undefined,
      },
      options.dialogTitle,
    ).then((result: {action?: string} | null) => ({
      // iOS's, and null here for the same reason it is null on Android: the
      // platform reports whether something was shared and not which service
      // took it.
      activityType: null,
      ...result,
    }));
  }

  /**
   * The content was successfully shared.
   */
  static sharedAction: 'sharedAction' = 'sharedAction';

  /**
   * The dialog was dismissed.
   */
  static dismissedAction: 'dismissedAction' = 'dismissedAction';
}

export default Share;
