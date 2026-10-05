/**
 * The modules these apps use that nothing here can supply types for.
 *
 * Two kinds, and neither is installable from this directory: `e2e/` has no
 * node_modules of its own, on purpose, so that every app runs against the
 * exact React Native the C++ was built against. The third-party ones are
 * borrowed from whatever application is being tested with, and the React
 * Native one is a private path with no published types.
 *
 * Narrow on purpose. Each declares the surface the app actually touches and
 * nothing else, so that the day an app reaches for something new it gets an
 * error here rather than silent `any`. That is the whole reason these files
 * are TypeScript: `any` by the back door would make the exercise pointless.
 *
 * @format
 */

/**
 * `@shopify/react-native-skia`, as `skia.tsx` uses it.
 *
 * Borrowed from the application rather than vendored, which is the arrangement
 * `skia.tsx` describes: there is no copy of it in this repository to read types
 * from even in principle.
 */
declare module '@shopify/react-native-skia' {
  import type * as React from 'react';
  import type {ViewProps} from 'react-native';

  /** What `readPixels` hands back, and what the app indexes into. */
  export type SkImage = {
    width(): number;
    height(): number;
    readPixels(): Uint8Array;
  };

  export type SkCanvasRef = {makeImageSnapshot(): SkImage | null};

  export function useCanvasRef(): React.RefObject<SkCanvasRef | null>;

  export const Canvas: React.ComponentType<
    ViewProps & {ref?: React.Ref<SkCanvasRef | null>; children?: React.ReactNode}
  >;
  export const Fill: React.ComponentType<{color?: string}>;
  export const Rect: React.ComponentType<{
    x: number;
    y: number;
    width: number;
    height: number;
    color?: string;
  }>;
}

/**
 * `expo-notifications`, as `notifications.tsx` uses it.
 *
 * Its native half is basalt-notifications, which implements this library's
 * contract rather than shipping a JavaScript API of its own, so the types that
 * matter here are the library's and the library is the application's.
 */
declare module 'expo-notifications' {
  export type PermissionResponse = {
    status: 'granted' | 'denied' | 'undetermined';
    canAskAgain: boolean;
    granted: boolean;
    /**
     * Why a desktop cannot show one, which basalt-notifications fills in and
     * expo's own implementations do not. Optional for that reason.
     */
    reason?: string;
  };

  export function getPermissionsAsync(): Promise<PermissionResponse>;
  export function requestPermissionsAsync(): Promise<PermissionResponse>;
  export function scheduleNotificationAsync(request: {
    content: {title?: string; subtitle?: string; body?: string};
    trigger: unknown;
  }): Promise<string>;
  export function getPresentedNotificationsAsync(): Promise<unknown[]>;
  export function dismissAllNotificationsAsync(): Promise<void>;
  /** Android's notification channels, which no desktop implements. */
  export function getNotificationChannelsAsync(): Promise<unknown[]>;
  export function setNotificationHandler(handler: unknown): void;
}

/**
 * React Native's own debugging overlay, which has no published types.
 *
 * `src/private/` is exactly what it says, and the app reaches into it because
 * that is where the component DevTools mounts actually lives. A private path
 * that moves should break this loudly, which it now will.
 */
declare module 'react-native/src/private/components/debuggingoverlay/specs/DebuggingOverlayNativeComponent' {
  import type * as React from 'react';
  import type {ViewProps} from 'react-native';

  export type TraceUpdate = {
    id: number;
    rectangle: {x: number; y: number; width: number; height: number};
    /** Packed ARGB, which is what the native side reads it as. */
    color: number;
  };

  export type ElementRectangle = {
    x: number;
    y: number;
    width: number;
    height: number;
  };

  export const Commands: {
    highlightTraceUpdates(view: unknown, updates: TraceUpdate[]): void;
    highlightElements(view: unknown, elements: ElementRectangle[]): void;
    clearElementsHighlights(view: unknown): void;
  };

  const DebuggingOverlayNativeComponent: React.ComponentType<
    ViewProps & {ref?: React.Ref<unknown>}
  >;
  export default DebuggingOverlayNativeComponent;
}

/**
 * Fabric's JSI binding, as `demo.tsx` drives it.
 *
 * That app exists to prove the mounting layer works with no React in it at
 * all, so it builds a shadow tree by hand against the binding React's own
 * renderer would use. Nothing types that binding: it is installed on the
 * runtime from C++, and the only description of it is React's renderer.
 *
 * Declared to the shape the app calls rather than fully. A node is opaque on
 * purpose, because nothing here looks inside one, and saying `unknown` is what
 * keeps that true.
 */
// Declared at the top level rather than inside `declare global`, which would
// need an `export {}` to be legal, which would make this file a module, which
// would turn every `declare module` above into an augmentation of something
// that does not exist. That is how the three of them silently stopped being
// found.

/** Opaque to everything here: created, cloned and handed back. */
type FabricNode = {surfaceId: number};
type FabricChildSet = unknown;

var nativeFabricUIManager:
  | {
      createNode(
        tag: number,
        viewName: string,
        surfaceId: number,
        props: object,
        instanceHandle: object,
      ): FabricNode;
      cloneNodeWithNewChildren(
        node: FabricNode,
        children: FabricChildSet,
      ): FabricNode;
      cloneNodeWithNewProps(node: FabricNode, props: object): FabricNode;
      appendChild(parent: FabricNode, child: FabricNode): void;
      /**
       * Takes nothing. UIManagerBinding registers it with an arity of 0 and
       * ignores whatever it is handed, which is worth stating because the
       * obvious guess is that it takes the surface id.
       */
      createChildSet(): FabricChildSet;
      appendChildToSet(set: FabricChildSet, child: FabricNode): void;
      completeRoot(surfaceId: number, set: FabricChildSet): void;
    }
  | undefined;

/** What the host calls to tear a surface down, installed by the app. */
var RN$stopSurface: ((surfaceId: number) => void) | undefined;

/** What the host calls to render a step of the demo, installed by the app. */
var basaltRender: ((surfaceId: number, step: number) => void) | undefined;

// React Native's hand-written global `Blob` (its src/types/globals.d.ts) lists
// only size/type/slice, but the class the runtime installs -- and that RN's own
// generated Libraries/Blob/Blob.d.ts describes -- also has `close()`. It is the
// one method that is RN's rather than the web's: nothing here is garbage
// collected, so a caller has to release the bytes itself. Merged in rather than
// cast at the call site, because it is a real method and the gap is upstream's.
interface Blob {
  close(): void;
}

// The TurboModule proxy the host installs on the global before the bundle runs.
// React Native calls it through `global.__turboModuleProxy` in its own
// TurboModuleRegistry and declares it nowhere a user bundle can see, so an app
// that looks for it directly -- which is the point of turbomodules.tsx -- has
// nothing to go on.
declare var __turboModuleProxy:
  | ((name: string) => object | null | undefined)
  | undefined;
