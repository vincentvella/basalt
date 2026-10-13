/**
 * React Native's own image view config, with the placeholder props declared.
 *
 * `<Image defaultSource>` is an image to draw until the real one arrives, and
 * on this platform it reached no host at all. The reason is that the two
 * halves of React Native's `<Image>` disagree, and a desktop takes one of each:
 *
 *   - `Image.js` is a self-importing shim, so it resolves to `Image.android.js`
 *     -- see the top of metro-config.ts for why Android's implementation is the
 *     one that matches what is implemented here. That file resolves the asset
 *     and sends a *string*: `nativeProps.defaultSource = defaultSource_.uri`,
 *     and `nativeProps.loadingIndicatorSrc` beside it.
 *   - `ImageViewNativeComponent.js` then branches on `Platform.OS ===
 *     'android'`, and a third platform takes the iOS branch. That branch
 *     declares `defaultSource: {process: resolveAssetSource}` and no
 *     `loadingIndicatorSrc` at all.
 *
 * `resolveAssetSource` returns an object unchanged and looks a *number* up in
 * the asset registry, so the string Android's JavaScript sent answers null:
 * the prop is processed into nothing. The other one is not in
 * `validAttributes`, so React never sends it. Both are dropped between two
 * files of React Native's, neither of which is wrong on its own platform.
 *
 * ReactCommon would have taken either -- `fromRawValue(..., ImageSource &)`
 * accepts a plain string as a remote uri -- so the whole fix in JavaScript is
 * to declare them without a `process`, the JavaScript above having already
 * done the resolving.
 *
 * ## Why this is nineteen lines and not two hundred
 *
 * backlog/image.md costed this as a fork of the whole file and declined it on
 * that basis: two hundred lines with two branches, every one of which would
 * become this project's to track. It does not have to be. The module exports
 * `__INTERNAL_VIEW_CONFIG`, and `NativeComponentRegistry.get` is handed a
 * *thunk* that is not called until the component is first rendered -- so the
 * config can be adjusted in place at import time, and everything else about
 * the file stays upstream's, including the branch that chooses it.
 *
 * The same shape as BaseViewConfig.ts beside this, and for the same reason:
 * what this platform is missing is a declaration, not an implementation.
 *
 * @format
 */

'use strict';

// Through this package's own upstream prefix rather than React Native's
// package path: both reach the same file, and the literal deep import is the
// one React Native warns about by name, in LogBox, in front of every app.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
const upstream: any = require('basalt-core/upstream/Libraries/Image/ImageViewNativeComponent');

const config = upstream.__INTERNAL_VIEW_CONFIG;
if (config?.validAttributes != null) {
  // `true`, not `{process: resolveAssetSource}`: Android's Image.js already
  // resolved the asset and sent a uri string, and ReactCommon parses a string
  // into an ImageSource. Running the iOS processor over it is what answered
  // null.
  config.validAttributes.defaultSource = true;
  // Android's spelling of the same idea, which the iOS branch never declares.
  // Sent as `loadingIndicatorSrc`, which is the name ReactCommon parses into
  // `ImageProps::loadingIndicatorSource`.
  config.validAttributes.loadingIndicatorSrc = true;
}

module.exports = upstream;
