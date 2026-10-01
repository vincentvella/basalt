/**
 * `<DropTarget>` -- a view the desktop may drop files or text onto.
 *
 * React Native has no API for this, because a phone has no pointer to drag
 * with. What a desktop application is expected to do is accept a file dropped
 * on it from the file manager, and this is that.
 *
 * ## How a view says so
 *
 * By its `nativeID`, which is the one prop a plain `<View>` carries to the
 * host on every platform -- so there is no registration call, no ref, and no
 * lifetime to get wrong. `<TitleBar.DragRegion>` marks views the same way.
 * The strings are written here and read in `native/core/DragAndDrop.h`, whose
 * tests round-trip them precisely because these two files cannot import each
 * other.
 *
 * It follows that a `<DropTarget>` cannot also carry an app's own `nativeID`.
 * That is a real limit and the honest one to take: the alternative was a
 * registration protocol whose failure mode is a target that silently stops
 * accepting after a re-render.
 *
 * ## What the handlers get
 *
 * `onDragOver` and `onDragLeave` are for the "you may drop here" state, and
 * carry no contents -- no desktop reveals what is being dragged until it
 * lands, and one that did should not tempt an app into reading it early.
 * `onDrop` carries the files and the text.
 *
 * @format
 */

import * as React from 'react';
import type {ViewProps} from 'react-native';
import {DeviceEventEmitter, TurboModuleRegistry, View} from 'react-native';

/** What was dropped. Both may be present; see native/core/DragAndDrop.h. */
export type DropPayload = {
  /** Absolute paths, not URIs. */
  files: string[];
  text: string;
};

export type DropPoint = {
  /** In the window's coordinates, like a touch. */
  x: number;
  y: number;
};

export type DropTargetProps = ViewProps & {
  /**
   * Which kinds this view takes. A drag offering none of them is not offered
   * to this view at all, so the cursor says no rather than the app refusing
   * after the fact.
   */
  accepts?: ReadonlyArray<'files' | 'text'>;
  onDragOver?: (point: DropPoint) => void;
  onDragLeave?: () => void;
  onDrop?: (payload: DropPayload, point: DropPoint) => void;
};

// Asked for, and the answer thrown away, because *getting* the module is what
// constructs it -- and its constructor is what registers the host-side drop
// listener. Without this an app that uses only <DropTarget> never instantiates
// BasaltWindows, the listener is never set, and every drop is found by the
// host, reported into a null std::function, and silently discarded. Which is
// exactly what happened: the C++ said "onto tag 18" and JavaScript heard
// nothing.
TurboModuleRegistry.get('BasaltWindows');

// Must match kDropTargetIdPrefix in native/core/DragAndDrop.h.
const MARKER_PREFIX = 'basalt-drop:';

// Must match kDropEvent in native/core/WindowsModule.h.
const DROP_EVENT = 'basaltDrop';

type NativeDropEvent = {
  tag: number;
  phase: 'over' | 'leave' | 'drop';
  x: number;
  y: number;
  files: string[];
  text: string;
};

/** The nativeID a view carries to accept these kinds. */
export function dropTargetId(accepts: ReadonlyArray<'files' | 'text'>): string {
  // Ordered rather than as given: the host parses either order, and a stable
  // string keeps the prop from changing identity on every render.
  const kinds: string[] = [];
  if (accepts.includes('files')) {
    kinds.push('files');
  }
  if (accepts.includes('text')) {
    kinds.push('text');
  }
  return MARKER_PREFIX + kinds.join(',');
}

export function DropTarget({
  accepts = ['files', 'text'],
  onDragOver,
  onDragLeave,
  onDrop,
  ...rest
}: DropTargetProps): React.ReactElement {
  // The tag is not known until the view is mounted, and the event carries it
  // -- so what the subscription matches on is the ref's current tag rather
  // than anything captured at render.
  const ref = React.useRef<React.ComponentRef<typeof View> | null>(null);

  // Handlers through refs, so an app passing new closures every render does
  // not re-subscribe.
  const handlers = React.useRef({onDragOver, onDragLeave, onDrop});
  handlers.current = {onDragOver, onDragLeave, onDrop};

  React.useEffect(() => {
    const subscription = DeviceEventEmitter.addListener(
      DROP_EVENT,
      (event: NativeDropEvent) => {
        // `__nativeTag`, with two underscores: that is what the new renderer
        // puts on a host component's ref. `_nativeTag` is the old one and is
        // simply undefined here, which made every event match nothing --
        // silently, because an event for another view is also no match.
        const tag = (ref.current as unknown as {__nativeTag?: number} | null)?.__nativeTag;
        if (tag == null || event.tag !== tag) {
          return;
        }
        const point = {x: event.x, y: event.y};
        if (event.phase === 'over') {
          handlers.current.onDragOver?.(point);
        } else if (event.phase === 'leave') {
          handlers.current.onDragLeave?.();
        } else {
          handlers.current.onDrop?.({files: event.files ?? [], text: event.text ?? ''}, point);
        }
      },
    );
    return () => subscription.remove();
  }, []);

  const nativeID = React.useMemo(() => dropTargetId(accepts), [accepts.join(',')]);

  return <View {...rest} ref={ref} nativeID={nativeID} />;
}
