/**
 * The window's title bar: its title, its colours, and whether the host draws
 * it or the app does.
 *
 *   useTitleBar({title, backgroundColor, textColor, borderColor, style})
 *   <TitleBar title="Inbox" backgroundColor="#1f1f1f" textColor="#fff" />
 *
 * `style: 'native'` (the default) keeps the system's title bar and colours it.
 * `style: 'hidden'` removes it: the app's own content reaches the top of the
 * window, the host still draws the minimise, maximise and close buttons over
 * the top right corner, and the app says which parts of its header drag the
 * window:
 *
 *   Window.minimize(); Window.toggleMaximize(); Window.close();
 *
 *   const {height, buttonsWidth} = useTitleBarMetrics();
 *   <TitleBar.DragRegion style={{height, paddingRight: buttonsWidth}}>
 *     <Text>Inbox</Text>
 *     <TitleBar.NoDragRegion><Button title="Compose" /></TitleBar.NoDragRegion>
 *   </TitleBar.DragRegion>
 *
 * Requests stack like <StatusBar>'s: the most recently mounted wins, key by key,
 * and unmounting one restores what was beneath it. See titleBarState.ts.
 *
 * All three hosts have a title bar module. The calls are guarded regardless:
 * on a host without one -- or before it has registered -- every call is
 * ignored and the metrics are all zero, so the same code runs everywhere.
 *
 * `<TitleBar.DragRegion>` is where the three are not yet equal. Windows and
 * GTK read the marks and drag the window from them; macOS does not read them
 * yet, so a header an app drew there shows correctly but does not move the
 * window. See core/TitleBarRegions.h for the marks and the rule.
 *
 * @format
 */

import * as React from 'react';
import type {TurboModule, ViewProps} from 'react-native';
import {DeviceEventEmitter, TurboModuleRegistry, View, processColor} from 'react-native';

import {createTitleBarStack, sameRequest} from './titleBarState';
import type {TitleBarColor, TitleBarOptions, TitleBarRequest, TitleBarStyle} from './titleBarState';

export type {TitleBarOptions, TitleBarStyle} from './titleBarState';

/**
 * What the host's `BasaltWindow` module offers, as far as this file uses it.
 *
 * Declared here rather than imported: there is no generated spec for it, and a
 * turbo module this platform may not have at all is better described by the
 * calls that are made on it than by a type that claims more.
 */
type NativeWindowModule = TurboModule & {
  setTitle(title: string | null): void;
  setTitleBarColors(
    background: number | null,
    text: number | null,
    border: number | null,
  ): void;
  setTitleBarStyle(style: TitleBarStyle): void;
  getTitleBarMetrics(): Partial<TitleBarMetrics> | null;
  minimize(): void;
  toggleMaximize(): void;
  close(): void;
  startWindowDrag(): void;
};

/** How much of the window a hidden title bar's caption takes, in layout units. */
export type TitleBarMetrics = {
  height: number;
  buttonsWidth: number;
  style: TitleBarStyle;
};

const NativeWindow = TurboModuleRegistry.get<NativeWindowModule>('BasaltWindow');

const METRICS_EVENT = 'basaltTitleBarMetricsChanged';

// Must match kTitleBarDragRegionId and kTitleBarNoDragRegionId in
// core/TitleBarRegions.h, which is where the hosts read them from -- Win32
// through Win32TitleBarLayout.h and GTK through GtkTitleBarLayout.h. They were
// spelled in the Windows host alone when Windows was the only reader; that
// header now takes its values from core rather than repeating them, so these
// two literals are the only other copy.
const DRAG_REGION_ID = 'basalt-titlebar-drag';
const NO_DRAG_REGION_ID = 'basalt-titlebar-no-drag';

const NO_METRICS: TitleBarMetrics = Object.freeze({
  height: 0,
  buttonsWidth: 0,
  style: 'native',
});

const stack = createTitleBarStack();
let applied: TitleBarRequest | null = null;

// processColor turns any React Native colour into the 0xAARRGGBB number the
// host reads. Null, and anything it cannot process, asks for the system's
// colour back.
function colorFor(value: TitleBarColor | undefined): number | null {
  if (value == null) {
    return null;
  }
  const processed = processColor(value);
  return typeof processed === 'number' ? processed : null;
}

function normalizeMetrics(value: Partial<TitleBarMetrics> | null | undefined): TitleBarMetrics {
  if (value == null) {
    return NO_METRICS;
  }
  return {
    height: value.height ?? 0,
    buttonsWidth: value.buttonsWidth ?? 0,
    style: value.style === 'hidden' ? 'hidden' : 'native',
  };
}

function applyStack(): void {
  if (NativeWindow == null) {
    return;
  }
  const request = stack.resolve();
  if (applied != null && sameRequest(request, applied)) {
    return;
  }
  const previous = applied ?? {};
  const first = applied == null;
  applied = request;

  if (first || request.title !== previous.title) {
    // Null restores the window's default title, which is the app's name.
    NativeWindow.setTitle(request.title == null ? null : String(request.title));
  }
  if (
    first ||
    request.backgroundColor !== previous.backgroundColor ||
    request.textColor !== previous.textColor ||
    request.borderColor !== previous.borderColor
  ) {
    NativeWindow.setTitleBarColors(
      colorFor(request.backgroundColor),
      colorFor(request.textColor),
      colorFor(request.borderColor),
    );
  }
  if (first || request.style !== previous.style) {
    NativeWindow.setTitleBarStyle(request.style === 'hidden' ? 'hidden' : 'native');
  }
}

export function useTitleBar(options?: TitleBarOptions | null): void {
  const {title, style, backgroundColor, textColor, borderColor} = options ?? {};
  const idRef = React.useRef<number | null>(null);

  // Mount and unmount. Layout effects, so a hidden title bar is in place before
  // the first frame is painted rather than one frame after it.
  React.useLayoutEffect(() => {
    const id = stack.push({title, style, backgroundColor, textColor, borderColor});
    idRef.current = id;
    applyStack();
    return () => {
      stack.remove(id);
      idRef.current = null;
      applyStack();
    };
    // Changes arrive through the effect below; this one only enters and leaves
    // the stack.
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, []);

  React.useLayoutEffect(() => {
    if (idRef.current == null) {
      return;
    }
    stack.update(idRef.current, {title, style, backgroundColor, textColor, borderColor});
    applyStack();
  }, [title, style, backgroundColor, textColor, borderColor]);
}

export function TitleBar(props: TitleBarOptions): null {
  useTitleBar(props);
  return null;
}

function DragRegion(props: ViewProps): React.ReactElement {
  return React.createElement(View, {...props, nativeID: DRAG_REGION_ID});
}

function NoDragRegion(props: ViewProps): React.ReactElement {
  return React.createElement(View, {...props, nativeID: NO_DRAG_REGION_ID});
}

TitleBar.DragRegion = DragRegion;
TitleBar.NoDragRegion = NoDragRegion;

function readMetrics(): TitleBarMetrics {
  if (NativeWindow == null) {
    return NO_METRICS;
  }
  try {
    return normalizeMetrics(NativeWindow.getTitleBarMetrics());
  } catch {
    return NO_METRICS;
  }
}

/**
 * What the caption buttons do, for an app drawing its own header.
 *
 * The system's buttons need none of this -- they are the system's -- but a
 * header the app drew has no way to act on itself otherwise, which makes the
 * hidden style a picture of a title bar rather than one.
 *
 * Every call is ignored on a host with no window module, like the rest of this
 * file, so the same code runs everywhere.
 */
export const Window = {
  minimize() {
    NativeWindow?.minimize();
  },
  toggleMaximize() {
    NativeWindow?.toggleMaximize();
  },
  close() {
    NativeWindow?.close();
  },
  /**
   * Starts dragging the window, to be called from a press on an app-drawn
   * caption. A no-op where the host moves windows some other way: GTK does it
   * through a widget rather than a call, so the drag regions handle it and
   * this has nothing to begin.
   */
  startDrag() {
    NativeWindow?.startWindowDrag();
  },
};

// How much of the window a hidden title bar's caption takes: its height, and
// the width of the buttons in the top right corner, both in layout units. All
// zero while the title bar is native. Updates when the style changes, the
// window moves to a display with a different DPI, or it is maximised.
export function useTitleBarMetrics(): TitleBarMetrics {
  const [metrics, setMetrics] = React.useState(readMetrics);

  React.useEffect(() => {
    if (NativeWindow == null) {
      return undefined;
    }
    const subscription = DeviceEventEmitter.addListener(
      METRICS_EVENT,
      (next: Partial<TitleBarMetrics> | null) => setMetrics(normalizeMetrics(next)),
    );
    // Anything that changed between the first render and subscribing.
    setMetrics(readMetrics());
    return () => subscription.remove();
  }, []);

  return metrics;
}
