/**
 * React Native's own base view config, plus three props it forgot to declare.
 *
 * `onPointerDown`, `onPointerUp` and `onPointerCancel` are registered in
 * `bubblingEventTypes` -- `topPointerDown` and friends, with their bubbled and
 * captured names -- and are supported the whole way down: `propsConversions.h`
 * parses them into `ViewProps::events`, `PointerEventsProcessor` handles them,
 * and `TouchEventEmitter::onPointerDown` exists to dispatch one.
 *
 * What is missing is one line in `validAttributes`, which lists
 * `onPointerEnter`, `onPointerLeave`, `onPointerMove`, `onPointerOut` and
 * `onPointerOver` and stops there. Without it React never sends the prop, so
 * `ViewProps::events` never has the bit, so `shouldEmitPointerEvent` returns
 * false and the event is dropped in C++ -- silently, and only for the three
 * that were left out.
 *
 * The effect on a phone is nothing, because no phone has a mouse button to
 * press. The effect here is that a right-click cannot be answered at all, which
 * is what this platform needed them for; see core/PointerButtons.h.
 *
 * Reported rather than forked: the whole file is React Native's, this adds five
 * keys, and it goes away when upstream adds them. See docs/BACKLOG.md.
 *
 * @format
 */

'use strict';

// Android's, for the reason every self-importing shim here is answered with
// Android's: these platforms report `PlatformConstantsAndroid` from C++, share
// ReactCommon's prop parsing, and drive the same components Android's
// JavaScript drives.
// A deep import into React Native, resolved by Metro and not through
// `exports`, so TypeScript cannot follow it -- hence the declaration rather
// than an import. The shape is React Native's own view config, for which it
// publishes no type.
//
// `.default` explicitly: that module is ESM and this is a `require`, so the
// interop an `import` would have done has to be done here. Without it the
// spread below copies a module namespace instead of the config, `validAttributes`
// is undefined, and every prop is dropped in JavaScript before it can reach
// C++. What that looks like from the outside is an app whose buttons have no
// accessibility role and whose fields cannot be focused -- which is how it was
// found.
// eslint-disable-next-line @typescript-eslint/no-explicit-any
//
// Through this package's own upstream prefix rather than by React Native's
// package path. Both reach the same file -- metro-config.ts turns the prefix
// into an absolute path -- but the literal specifier is a deep import into
// another package's internals, and React Native warns about those by name. That
// warning opened LogBox in front of every app on start-up, for a file the app
// did not write and cannot change, which is the whole of the reason the prefix
// exists. The other overrides in this directory already use it; this one was
// missed.
const upstream: any = require('basalt-core/upstream/Libraries/NativeComponent/BaseViewConfig.android');
const BaseViewConfig = upstream.default ?? upstream;

export default {
  ...BaseViewConfig,
  validAttributes: {
    ...BaseViewConfig.validAttributes,
    // The three registered events upstream does not declare, with their capture
    // phases. `onPointerCancel` is here for completeness rather than because
    // anything emits one yet: a desktop pointer is cancelled by the window
    // losing capture, which the hosts do not report.
    onPointerDown: true,
    onPointerDownCapture: true,
    onPointerUp: true,
    onPointerUpCapture: true,
    onPointerCancel: true,
    onPointerCancelCapture: true,
  },
};
