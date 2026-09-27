/**
 * react-native-basalt
 *
 * What this platform adds to React Native, and nothing else. Components come
 * from `react-native` exactly as they do on iOS and Android; this is where the
 * few things with no React Native equivalent live.
 *
 * It used to re-export all of `react-native` as well, and that is worth saying
 * why it no longer does, because the failure was quiet until something imported
 * this file. Babel compiles `export * from 'react-native'` into a loop that
 * reads every one of React Native's exports once -- and most of them are lazy
 * getters, which run their module's top level when read. So importing this
 * package loaded every deprecated component (and printed every deprecation
 * warning), and loaded DevMenu, whose getEnforcing throws on a host with no
 * DevMenu module. That throw aborted the importing module, which in an Expo app
 * is App.js, and the app stopped at "App entry not found". Nothing had imported
 * this file before the title bar gave apps a reason to.
 *
 * @format
 */

'use strict';

// Extensionless on purpose: Metro picks Platform.linux.js, Platform.macos.js or
// Platform.windows.js by the platform being bundled for, which is the same
// mechanism React Native uses for its own and means this line never has to know
// how many desktops there are.
export {default as Platform} from './overrides/Platform';
// The native file dialogs, which React Native has no API for: a phone has none.
// See useDialog.js.
export {useDialog, dialog} from './useDialog';
export type {Dialog, DialogFilter, DialogOptions, DialogResult} from './useDialog';
// The application menu. On macOS it is also what makes Cmd-C reach a text
// field, which is why an app gets one whether or not it renders a <Menu>. See
// Menu.js.
export {Menu} from './Menu';
export type {MenuItemProps, MenuProps, MenuRole, MenuSubmenuProps} from './Menu';
// A context menu, which every desktop has -- including the one with no menu
// bar. See useContextMenu.js.
export {useContextMenu, contextMenu} from './useContextMenu';
export type {ContextMenu, ContextMenuItem, ContextMenuPoint} from './useContextMenu';
// A second window, which is a second surface and so a second React root. See
// Window.js for what that costs and what it does not.
export {Window} from './Window';
export type {WindowProps} from './Window';
// The window itself: its size, how big it may be, its position, whether it
// fills the screen, live bounds, and being asked before it closes. See
// useWindow.js.
export {useWindow, useCloseRequest, windowControl} from './useWindow';

// Being asked before the *application* quits, which the window half above
// does not cover: see src/quitRequest.ts.
export {useQuitRequest, quit} from './quitRequest';

// What screens the desktop has, which React Native's Dimensions does not
// answer: see src/useDisplays.ts.
export {useDisplays, displays, primaryDisplay, pointerPosition} from './useDisplays';

// Accepting a file or text dragged onto the app, which React Native has no
// API for: see src/DropTarget.tsx.
export {DropTarget, dropTargetId} from './DropTarget';
// Keyboard shortcuts on a view, which React Native has only inside a
// <TextInput>. See src/KeyHandler.tsx.
export {KeyHandler} from './KeyHandler';
export {DragSource, dragSourceId} from './DragSource';
export type {DragSourceProps} from './DragSource';
export type {DropPayload, DropPoint, DropTargetProps} from './DropTarget';
export type {HandledKey, KeyHandlerProps, PressedKey} from './KeyHandler';
export type {Display, PointerPosition} from './useDisplays';
export type {AllowQuit, QuitRequestHandler} from './quitRequest';
export type {
  CloseRequestHandler,
  UseWindow,
  WindowBounds,
  WindowCapabilities,
  WindowControl,
  WindowId,
} from './useWindow';
// The window's title bar: its title and colours, and a hidden style that lets
// the app draw its own header. See TitleBar.js.
export {TitleBar, useTitleBar, useTitleBarMetrics} from './TitleBar';
export type {TitleBarMetrics, TitleBarOptions, TitleBarStyle} from './TitleBar';
