/**
 * `<DragSource>` -- a view that can be dragged out of the app.
 *
 * The other direction from `<DropTarget>`, and the half people forget: a file
 * manager needs it, and so does anything that lets you drag a row into another
 * application.
 *
 * ## Why the payload is a prop and not a callback
 *
 * Every toolkit owns the gesture. `GtkDragSource`,
 * `beginDraggingSessionWithItems:` and `DoDragDrop` all start the drag
 * themselves and ask what is being dragged *synchronously*, on the UI thread.
 * An app asked at that moment would have to answer from JavaScript on another
 * thread -- the same problem `useCloseRequest` has, solved the same way: say it
 * beforehand.
 *
 * So this is declarative. A view says what it represents, and the drag carries
 * that. Changing the prop changes what the next drag carries; there is no
 * moment at which an app is consulted.
 *
 * ## What can be dragged
 *
 * A file path or a line of text, which are the two every desktop understands.
 * A file is offered as a file URL, so another application receives it as a
 * file rather than as its name.
 *
 * Like `<DropTarget>`, this uses `nativeID` and therefore cannot also carry an
 * app's own -- see that file for why the trade is worth it.
 *
 * @format
 */

import * as React from 'react';
import type {ViewProps} from 'react-native';
import {View} from 'react-native';

export type DragSourceProps = ViewProps & {
  /** An absolute path. Dragged out as a file URL. */
  file?: string;
  /** A line of text. Ignored when `file` is set. */
  text?: string;
};

// Must match kDragSourceIdPrefix in native/core/DragAndDrop.h.
const MARKER_PREFIX = 'basalt-drag:';

/**
 * The nativeID a view carries to be draggable.
 *
 * Files win over text where both are given, which is what the host does too:
 * a desktop dragging a file offers its name as text anyway, so the file is the
 * more specific answer.
 */
export function dragSourceId({file, text}: {file?: string; text?: string}): string {
  if (file != null && file !== '') {
    return `${MARKER_PREFIX}files:${file}`;
  }
  if (text != null && text !== '') {
    return `${MARKER_PREFIX}text:${text}`;
  }
  // No payload: a marker the host reads as nothing, so the view is simply not
  // draggable. Better than omitting the prop, which would leave a stale
  // marker from a previous render.
  return MARKER_PREFIX;
}

export function DragSource({file, text, ...rest}: DragSourceProps): React.ReactElement {
  const nativeID = React.useMemo(() => dragSourceId({file, text}), [file, text]);
  return <View {...rest} nativeID={nativeID} />;
}
