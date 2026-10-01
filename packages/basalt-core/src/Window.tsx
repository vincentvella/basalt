/**
 * A second window.
 *
 *   <Window title="Preview" width={600} height={400} onClose={hide}>
 *     <Preview id={id} />
 *   </Window>
 *
 * Mount it and a window opens; unmount it and the window closes. React Native
 * has no API for this because a phone has one window and it is the screen.
 *
 * ## A window is a React root
 *
 * That is Fabric's grain rather than a decision this makes: a surface is what
 * has a size, a layout context and a root shadow node, and two windows sharing
 * one would be two windows sharing a layout. So a second window runs a second
 * React tree, and `<Window>`'s children are rendered *there* rather than where
 * they were written.
 *
 * Which has a consequence worth stating plainly, because it is the one thing
 * that will surprise people: **the children do not see React context from the
 * tree they were written in.** A theme provider, a navigation container or a
 * Redux store wrapping the `<Window>` is not wrapping its children. What does
 * cross is props -- the elements themselves are passed through, so anything
 * captured in a closure or given as a prop arrives intact, including state and
 * callbacks:
 *
 *   const [count, setCount] = useState(0);
 *   <Window><Text onPress={() => setCount(count + 1)}>{count}</Text></Window>
 *
 * works, and re-renders the window when `count` changes. A `useContext` inside
 * `<Preview>` does not. An app that needs context in a window should pass the
 * value as a prop and re-provide it inside.
 *
 * ## `onClose`
 *
 * Called when the *person* closed the window -- its own close button, or the
 * window manager -- and not when the app unmounted the `<Window>`. An app that
 * unmounted it already knows.
 *
 * It is the half that keeps the two sides agreeing: without it an app's `open`
 * flag would stay true after the window had gone, the next render would try to
 * close a window that closed itself, and the window could never be reopened.
 *
 * ## `onCloseRequest`
 *
 *   <Window onCloseRequest={close => (dirty ? ask() : close())}>
 *
 * The other half of `onClose`, and the earlier one: somebody is *trying* to
 * close the window and it has not closed. A `<Window>` with this prop refuses
 * every close the window manager asks for and reports the attempt instead, so
 * the app can ask "are you sure".
 *
 * There are two ways to then say yes, and they are not the same one written
 * twice. Unmounting the `<Window>` is the React-shaped one and usually right --
 * the window is a render of some state, and agreeing to close it is that state
 * changing. The `close` the handler is handed is the other, for when the app
 * has no such state to change, and it closes the window without `onClose`,
 * because an app that just said yes is not waiting to be told.
 *
 * `onClose` is unaffected by any of this: it still means the person closed the
 * window, which now only happens when there is no handler to refuse it.
 *
 * `useWindow()` inside a second window's tree still reports the *active*
 * window, not the one it is in; that is a real gap and is in docs/BACKLOG.md.
 *
 * @format
 */

'use strict';

import * as React from 'react';
import type {TurboModule} from 'react-native';
import {AppRegistry, DeviceEventEmitter, TurboModuleRegistry} from 'react-native';

import {useCloseRequestFor} from './closeRequest';
import type {CloseRequestHandler, WindowId} from './closeRequest';

/** How a second window opens. Changing these afterwards is `useWindow()`'s job. */
export type WindowProps = {
  title?: string;
  width?: number;
  height?: number;
  children?: React.ReactNode;
  /** The person closed it. The window is already gone when this arrives. */
  onClose?: () => void;
  /** Be asked first. See closeRequest.ts. */
  onCloseRequest?: CloseRequestHandler | null;
};

type OpenWindowRequest = {
  component: string;
  title: string;
  width: number;
  height: number;
  props: {basaltWindowId: number};
};

type NativeWindowsModule = TurboModule & {
  open(request: OpenWindowRequest): Promise<WindowId | null>;
  close(id: WindowId): void;
};

const NativeWindows = TurboModuleRegistry.get<NativeWindowsModule>('BasaltWindows');

/** Whether this platform can open a second window at all. */
export const isSupported = NativeWindows != null;

// What each open window is rendering, by the id `<Window>` gave it, and who to
// tell when it changes. Module level because the two ends are in different
// React roots and have no ancestor in common -- which is the whole situation.
// Must match kWindowClosedEvent in native/core/WindowsModule.h.
const CLOSED_EVENT = 'basaltWindowClosed';

const contents = new Map<number, React.ReactNode>();
const listeners = new Map<number, (children: React.ReactNode) => void>();

let nextId = 1;

function setContent(id: number, children: React.ReactNode): void {
  contents.set(id, children);
  const listener = listeners.get(id);
  if (listener != null) {
    listener(children);
  }
}

/**
 * The component every second window runs.
 *
 * Registered once, under a name an app will not collide with. It renders
 * whatever its `<Window>` currently has as children, and subscribes so that a
 * re-render of the first tree reaches the second.
 */
function WindowContent({basaltWindowId}: {basaltWindowId: number}): React.ReactNode {
  const [children, setChildren] = React.useState<React.ReactNode>(
    () => contents.get(basaltWindowId) ?? null,
  );

  React.useEffect(() => {
    listeners.set(basaltWindowId, setChildren);
    // Read again on mount: the first tree may have re-rendered between the
    // window opening and this root evaluating, and the map is the truth.
    setChildren(contents.get(basaltWindowId) ?? null);
    return () => {
      listeners.delete(basaltWindowId);
    };
  }, [basaltWindowId]);

  return children;
}

const CONTENT_COMPONENT = 'BasaltWindowContent';
let registered = false;

function ensureRegistered(): void {
  if (registered || !isSupported) {
    return;
  }
  registered = true;
  AppRegistry.registerComponent(CONTENT_COMPONENT, () => WindowContent);
}

export function Window({
  title,
  width,
  height,
  children,
  onClose,
  onCloseRequest,
}: WindowProps): null {
  const id = React.useRef<number | null>(null);
  if (id.current == null) {
    id.current = nextId++;
  }
  const windowId = id.current;

  // Written during render rather than in an effect, so that the second root
  // never renders a frame behind the first. Rendering is not supposed to have
  // effects; this one is a write to a map the other root reads, which is the
  // narrowest version of what a portal does.
  setContent(windowId, children);

  // The surface id the host gave us, which is what closes the window. Kept in a
  // ref rather than in state: nothing renders differently for it.
  const surfaceId = React.useRef<WindowId | null>(null);
  // And the same id in state, which `onCloseRequest` needs. A ref changing is
  // not a render, and the intercept has to be registered once the host has
  // answered with an id -- which is a promise later than the effect that asked
  // for the window. Two homes for one number, because the two readers want
  // different things from it.
  const [opened, setOpened] = React.useState<WindowId | null>(null);

  // In a ref so that the subscription below does not have to be torn down and
  // rebuilt every time an app passes a new closure, which is every render.
  const onCloseRef = React.useRef(onClose);
  onCloseRef.current = onClose;

  // Deliberately not depending on title, width or height. They are how a window
  // *opens*, and re-opening one because its title changed would be a new window
  // in a new place. Changing them afterwards is `useWindow()`'s job.
  // Closed by the person rather than by the app -- its own close button, or the
  // window manager. The window is already gone by the time this arrives; what
  // it is for is telling the app, so its state stops saying the window is open.
  //
  // Without it the next render would try to close a window that closed itself,
  // and an app whose `open` flag never cleared could never reopen one.
  React.useEffect(() => {
    if (!isSupported) {
      return;
    }
    const subscription = DeviceEventEmitter.addListener(CLOSED_EVENT, closedId => {
      if (closedId !== surfaceId.current) {
        return;
      }
      // Forgotten first, so the cleanup below does not ask the host to close a
      // window that is already gone.
      surfaceId.current = null;
      setOpened(null);
      if (onCloseRef.current != null) {
        onCloseRef.current();
      }
    });
    return () => subscription.remove();
  }, []);

  // Asked before it closes, if the app wants to be. See closeRequest.js.
  useCloseRequestFor(
    () => opened,
    // `isSupported` is what proves the module is here.
    (closing: WindowId) => NativeWindows!.close(closing),
    onCloseRequest,
    [opened],
  );

  React.useEffect(() => {
    if (!isSupported) {
      return;
    }
    ensureRegistered();

    let closed = false;
    NativeWindows!.open({
      component: CONTENT_COMPONENT,
      title: title ?? '',
      width: width ?? 900,
      height: height ?? 700,
      props: {basaltWindowId: windowId},
    }).then((openedId: WindowId | null) => {
      // Unmounted while the window was opening, which is a race a fast app
      // really does lose: close what was opened rather than leaking it.
      if (closed) {
        if (openedId != null) {
          NativeWindows!.close(openedId);
        }
        return;
      }
      surfaceId.current = openedId;
      setOpened(openedId);
    });

    return () => {
      closed = true;
      if (surfaceId.current != null) {
        NativeWindows!.close(surfaceId.current);
        surfaceId.current = null;
      }
      contents.delete(windowId);
      listeners.delete(windowId);
    };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [windowId]);

  return null;
}

Window.isSupported = isSupported;
