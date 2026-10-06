# Desktop capabilities

Part of the [backlog](../BACKLOG.md). Not scheduled.

An entry here is a gap, not a plan. When one is picked up it becomes a change in
`openspec/changes/`, which is where the requirements and the task list live from
then on: the entry stays, so the catalogue remains a complete answer to "what
is missing", and gains a pointer. Three entries have one today.

**Open (32):**

1. A second window's children do not see React context
2. `useWindow()` inside a second window reports the active window
3. The title bar and the error inspector are the main window's
4. `<Modal>` is still an in-surface overlay
5. Packaging is macOS and Linux only, and shallow
6. A window cannot be positioned on Linux
7. A window's lifecycle is an app's to influence now
8. Menus have no checkbox or radio items, and no dynamic enabling
9. A system tray icon
10. Windows notifications carry no identity of their own
11. Cursor control
12. Application lifecycle; refusing to quit is proposed
13. ~~Drag and drop~~; both directions; dragging out is untestable and says so
14. Clipboard
15. Shell integration
16. ~~Displays and screen~~, done
17. Power and idle
18. Global shortcuts
19. Permissions
20. Secure storage
21. Auto-update
22. Printing
23. File system beyond the dialogs
24. Screen capture and media devices
25. Crash reporting
26. Window state is not remembered between launches
27. No progress in the taskbar or dock
28. A window cannot represent a file
29. No vibrancy, transparency or window shape
30. No online and offline detection
31. No spell checking
32. No dock, launcher or jump-list menu
33. No way to do work off the JavaScript thread

React Native has no cross-platform API for any of this, because it was built for
phones. That makes each one a design question before it is an implementation
question: invent a `basalt-core` API, follow what react-native-macos or
react-native-windows already chose, or leave it to userland modules. Nothing
here has been decided, and none of it is needed for the demo, which is why it
has gone unrecorded until now.

- ~~**More than one window**~~, and ~~**a window that can refuse to close**~~.
  Done on all three: `<Window>` opens one, `useCloseRequest()` guards it. A
  window is a surface is a React root, which is Fabric's grain rather than a
  simplification, and the refusal is a registration rather than a returned
  `false`, because the window manager wants a synchronous answer and the handler
  is on another thread, so the decision has to exist before the attempt.

  The four entries below are what follows from that and are still open. Refusing
  to *quit* is a different event and is under Application lifecycle in the
  catalogue.
- **A second window's children do not see React context** from the tree they
  were written in, because they are rendered in a different root. Props, state
  and callbacks cross; a provider does not. `<Window>`'s header says so.
- **`useWindow()` inside a second window reports the active window**, not the
  one it is in. A TurboModule has no idea which surface called it, which is the
  same gap that would have to be closed for per-window menus or title bars. The
  size limits are the sharpest version: they are one set of numbers for one
  window, so Windows answers WM_GETMINMAXINFO for the app's own window and no
  other, to keep the three hosts saying the same thing.
- **The title bar and the error inspector are the main window's.** Both are
  process-wide seams; making them per-window is its own piece of work.
- **`<Modal>` is still an in-surface overlay**, though a real window is now
  something this platform could do.
- **Packaging is macOS and Linux only, and shallow.** `react-native run-macos`
  builds a real `.app`: `Info.plist`, an identifier, declared URL schemes, and
  an ad-hoc signature, which macOS requires before it will grant notification
  permission at all. What it does not do is an icon (`.icns` needs `iconutil`),
  a real signing identity, notarisation, or a `.dmg`. `run-linux` writes a
  `.desktop` file and does not install it: putting a file in
  `~/.local/share/applications` is a change to the session that a build command
  should not make on its own, and there is no `.deb`, `.rpm` or Flatpak.
  Windows writes a per-user Start Menu shortcut carrying an AppUserModelID, at
  startup rather than from the CLI, and has no installer, no `.msi` and no code
  signature.
- **A window cannot be positioned on Linux**, and that is the end of it rather
  than a gap. The rest of window geometry is done: `useWindow()` gives an app
  `setSize`, `setPosition`, `center`, `setFullScreen`, `minimize`,
  `toggleMaximize`, `close`, and live `bounds`. What is left is the part GTK4
  will not do at all: `setPosition` and `center` are no-ops on Linux and
  `bounds.x` is always zero there, because `gtk_window_move` is gone and Wayland
  has no equivalent. The rest of the lifecycle is done: a close attempt is
  reported, so an app can ask "are you sure", and `setMinimumSize`,
  `setMaximumSize`, `setResizable` and `setAlwaysOnTop` are there with
  `capabilities` saying which of them this desktop actually does: on Linux the
  maximum and always-on-top are `false`, because GTK4 removed both calls and
  Wayland has no protocol for either.
- **A window's lifecycle is an app's to influence now**, `useCloseRequest()`
  and `<Window onCloseRequest>` refuse the close and report the attempt, which
  is where "are you sure" goes. So is how big it may be: a minimum, a maximum,
  a resizable flag and always-on-top, with `capabilities` answering which of
  those this desktop does. What is left of the lifecycle is **quitting**, which
  is a different event from closing a window: Cmd-Q, and the Windows and GNOME
  session-end signals, each need their own seam. Proposed, as
  `openspec/changes/refuse-to-quit`.
  The title can be influenced too, with the title bar's colours and a hidden
  style that lets the app draw its own header: `useTitleBar`, `<TitleBar>`,
  `<TitleBar.DragRegion>` and `useTitleBarMetrics`. Implemented on all three:
  GTK drops its decorations and takes a header from the app, AppKit uses a
  transparent full-size-content title bar that keeps the traffic lights, and
  Win32 draws the caption buttons over the app's own header. See the
  `window-title-bar` spec.
- **Menus have no checkbox or radio items, and no dynamic enabling.** The
  application menu itself is done where a platform has one: `<Menu>`
  with `<Menu.Item role="copy" />`, over NSMenu and an HMENU. `Menu.isSupported`
  is false on Linux and is not a gap: GNOME's guidelines have said to use a
  header bar with a menu button since GNOME 3, and GTK4 removed the widget.
  Context menus are done too, on all three: `useContextMenu().show(items,
  where)`, which answers with the index chosen or null. That one is *more*
  portable than the menu bar rather than less: a popup is something every
  desktop has always had, including the one with no menu bar.

  A right-click opens one now, on all three: a secondary click arrives as
  `onPointerDown` with `button === 2` and does *not* fire `onPress`, which is
  what it means on every desktop. See core/PointerButtons.h.

  What is left: no checkbox or radio items; no dynamic enabling without
  re-rendering the whole menu; and the role labels are English, because nothing
  here is localised.

  ~~**A popup takes neither a submenu nor a role**~~, and until 2026-10-05 the
  TypeScript said otherwise: `ContextMenuItem` declared `role`, `submenu` and
  `accelerator`, while `entriesFrom` in core/MenuModule.cpp read `label`,
  `enabled`, `shortcut` and `separator` and nothing else. Three fields that did
  nothing, with no error. The type was corrected first, in da88247.

  **Both are done**, 2026-10-06. See
  openspec/changes/archive/2026-10-06-nest-and-role-the-popup-menu. The entry
  below is left as it was written, because the implementation followed it and
  because one line of it was wrong in a way worth keeping:

  *Submenus are a protocol choice, not a platform limit.* All three popups are
  built from primitives that nest: `NSMenu` has `item.submenu`, GMenu has
  `g_menu_append_submenu`, an HMENU takes `MF_POPUP`. What does not survive
  nesting is the answer. `showMenu` resolves with a position in a flat vector,
  which is how each host tags its items today (AppKit `item.tag = i`, Win32
  `index + 1`, GTK an action name per index), and a tree has no single position.
  The menu bar already solved this: `MenuItemModel` carries an `id`. Giving
  `MenuEntry` one too is the work, across three hosts and the promise in
  useContextMenu.ts.

  > What it took instead: no id. An index became a position in a pre-order walk
  > of the whole menu, which for a flat list is the position in the vector, so
  > nothing a caller relied on moved and the hosts kept the tagging they had.
  > One numbering, defined once in `walkMenuEntries`.

  *Roles in a popup are further off, and may not be wanted.* On AppKit they are
  nearly free, since AppKitMenuBar.mm already maps a role to a selector with a
  `nil` target and that works the same in a popup; Win32 has `commandForRole`
  posting to the focused control. Linux has nothing to reuse, because it has no
  menu bar at all: `applicationMenuSupported()` is false and
  `setApplicationMenu` is deliberately empty. The harder half is the API. A role
  is performed by the platform, so nothing comes back, and a promise documented
  to answer "the index chosen, or null if dismissed" has no honest third thing
  to say. That is a decision about what `show()` resolves with, and it should be
  made before any of it is built.

  > The decision: nothing. A role reports its index like any other item, because
  > the popup performs the role itself rather than handing the item a `nil`
  > target the way the bar does. Both halves happen and the promise keeps the two
  > outcomes it had. Linux did need writing from nothing, and got twelve of the
  > thirteen.

  What is still open from this entry: checkbox and radio items, dynamic enabling,
  English-only role labels, and `about` on Linux and Windows.

  The thing the work added rather than closed is now half shut. A role's
  behaviour is asserted, with `close`, on every host: see "a role in a context
  menu performs it, not only reports it", which also runs the ordinary item beside
  it so that a host dying early cannot pass. What is still unasserted is a *text*
  role, because nothing here can focus a field before a tap opens the menu, and
  the menu bar's roles are unasserted behaviourally for the same kind of reason.
  Both are in that archived change's notes, with what was tried.
- ~~**Native file dialogs.**~~ Done on all three: `useDialog().openFile()`,
  `saveFile()` and `openFolder()`, over `GtkFileDialog`, `NSOpenPanel` /
  `NSSavePanel` and `IFileDialog`. What is left is the rest of what a desktop
  calls a dialog: a message box with arbitrary buttons (`Alert` is limited to
  three on Windows, and has no text field on Linux; see the core modules
  section), and no API for a print or colour dialog.
- **A system tray icon.** Half done, and not as a feature: `Win32Notifications.cpp`
  owns a hidden one because `Shell_NotifyIcon` needs an icon to notify from.
  What an app cannot do is put its own icon there, give it a tooltip, a menu or
  a click handler, which is what the feature would be. Nothing equivalent exists
  on GTK or AppKit.
- **Windows notifications carry no identity of their own.** `Shell_NotifyIcon`
  shows one at a time per icon and replaces whatever was there, so
  `dismissNotification` can only take down the current one and
  `presentedNotifications` answers what this process last asked for. A person
  who has already dismissed one still sees it counted as presented. The fix is
  `ToastNotificationManager`, which means WinRT, a manifest and a registered
  activator COM class.
- **Cursor control** beyond what the `cursor` style property covers: setting a
  busy cursor for the window, hiding it, and capturing it to a region.

### The rest of the surface, catalogued

Everything above grew out of something the demo or an app needed, which is why
it is mostly windows, menus and dialogs: those are what came up. This is the
other direction: Electron's main-process surface and the consent dialogs a
desktop actually shows, gone through one at a time and checked against the
repository rather than remembered. Status is *done*, *partial* or *absent*, and
partial always says which half.

Nothing here is scheduled, and the three entries that carry a pointer are
proposed rather than started. It is here so that "what is missing" is an answer
rather than a search.

Where these land when they are built is its own question, and a decided one:
`openspec/changes/split-optional-capabilities-into-packages` draws the line
between what core owes every app and what belongs in a capability package. It
was written against this catalogue, so a new entry here is also a new entry for
that boundary to place.

- **Application lifecycle**, *absent*, all of it.
  - **Quitting cannot be refused.** The gap `useCloseRequest()` leaves: an app can
  guard every window and still lose data to Cmd-Q, which goes through
  `applicationShouldTerminate:` and never asks a window whether it minds. The
  Windows and GNOME session-end signals (`WM_QUERYENDSESSION`, the session
  manager's) are the same question. Each needs its own seam; the flag machinery
  in core/WindowHost.h is the shape to copy. Proposed, as
  `openspec/changes/refuse-to-quit`.
  - **No single-instance lock.** A second launch starts a second process. Every
  desktop expects the first to be raised and handed the arguments instead,
  which is also how a file association or a URL reaches a running app.
  - **No launch at login**, no recent-documents list, no dock or taskbar badge,
  no jump list, and no standard About panel.


- **Drag and drop**; *dropping in works on all three; dragging out is
  absent*. A view marks itself with `nativeID` and is told what was dropped on
  it, over `GtkDropTarget`, `NSDraggingDestination` and OLE's `IDropTarget`,
  with the hit test and the "innermost marked view wins" rule in
  `core/DragAndDrop.h`.

  Dragging *out* works too, through `GdkContentProvider`,
  `NSPasteboardWriting` and `DoDragDrop`. It needed the payload declared in
  advance rather than asked for, because every toolkit owns the gesture and
  asks what is being dragged synchronously on the UI thread, the same
  constraint refusing to close has.

  It has no automated coverage and cannot: the system owns the drag once it
  starts, and no instrument can put a file manager on the other end. What is
  proven is that a drag source is inert when nothing is marked; the rest is
  the demo and a person. Windows needed by far the most code: OLE has no
  simple data object, so one dragged file means a hand-written `IDataObject`,
  an `IDropSource` and `SHCreateStdEnumFmtEtc`.
- **Clipboard**, *partial*. Text works, through React Native's own `Clipboard`.
  Images, HTML, RTF and a list of files are each a separate pasteboard type on
  each platform, and none is carried.

  On Linux there is a second limit, and it is structural rather than missing
  work: `clipboardText` sees only this application's own clipboard.
  `GdkClipboard` reads asynchronously; it may have to ask another process,
  and the seam is synchronous because `Clipboard.getString()` resolves at once.
  Reading another application's clipboard needs an async path core does not hand
  down. The file says so plainly rather than returning an empty string, which
  would look like an empty clipboard.

- **Shell integration**, *partial*. Opening a URL works (`Linking`). Opening a
  path with its default application, revealing a file in the file manager, and
  moving one to the trash do not. Custom URL schemes are declared by
  `app.identity.json` and `Linking.getInitialURL()` answers on a cold start, but a
  URL delivered to an app that is *already running* is not reported, which needs
  the single-instance lock above to be anywhere to deliver it to.

- ~~**Displays and screen**: *partial, and not exposed at all*.~~ Done:
  `useDisplays()`, `displays()`, `primaryDisplay()` and `pointerPosition()`,
  each display carrying its bounds, work area, scale factor and whether it is
  primary, plus an event when the arrangement changes. The lookups did already
  exist for `center()` and full screen; what this added was passing them on.

  **Two of the four are unanswerable on GTK**, and not because nobody wrote
  them. `gdk_monitor_get_workarea`, `gdk_display_get_primary_monitor` and
  reading the pointer were all GTK 3 APIs, removed rather than overlooked:
  Wayland has no protocol for a work area a client can read, no notion of a
  primary output, and tells a client where the pointer is only while it is
  over that client's own surfaces. So GTK reports the work area as the full
  bounds, the first monitor as primary, and the pointer as unknown, which is
  why `PointerPosition` carries a `known` flag rather than a bare point.

- **Power and idle**, *absent*. Suspend, resume, lock, unlock, on-battery and
  battery level; how long the person has been idle; and asking the system not to
  sleep while something is running. The last is the one a media or build app needs
  and cannot fake.

- **Global shortcuts**, *absent*. A menu accelerator works while the app is
  focused, which is a different thing from a shortcut that works when it is not.

- **Permissions**, *absent as a concept*, which matters more than any single one
  of them. macOS gates the camera, the microphone, screen recording, location,
  accessibility and full disk access behind TCC, and each needs a usage string in
  `Info.plist` *and* a request at runtime; Windows has its own capability prompts.
  Today `cli/packageApp.js` writes no usage strings and nothing asks for anything,
  so an app that reaches for a camera is denied without a prompt. Notifications
  are the one consent flow that works, and only because expo-notifications drove
  it. What is missing is the seam, "ask for X, tell me the answer, tell me when
  it changes", rather than any particular permission.

- **Secure storage**, *absent*. No Keychain, Credential Manager or libsecret, so
  an app storing a token has nowhere but a file.

- **Auto-update**: *absent*, and worth deciding rather than building: it is
  Sparkle on macOS, MSIX or a custom updater on Windows, and the package manager
  on Linux, which is three answers rather than one API.

- **Printing**, *absent*. No print dialog and no page rendering.

- **File system beyond the dialogs**, *absent*. No watching a directory, and no
  security-scoped bookmarks, which is how a sandboxed macOS app keeps access to a
  file the person chose last week.

- **Screen capture and media devices**, *absent*. No display or window capture,
  no camera or microphone enumeration.

- **Crash reporting**, *absent*. A host that segfaults leaves an `.ips` on macOS
  and nothing an app or its author sees.

  The eight below came from reading Electron's own contents page against this
  list, which is worth doing once more than never: each is something a desktop
  app routinely does and none of them had been written down.

- **Window state is not remembered between launches**, *absent*. Every desktop
  app reopens where it was, the size it was, maximised if it was. `useWindow()`
  has every piece needed to do it by hand and no app should have to.
- **No progress in the taskbar or dock**, *absent*. A determinate or
  indeterminate bar on the dock icon or the taskbar button, which is what a
  download or an export is expected to show.
- **A window cannot represent a file**, *absent*. macOS puts a proxy icon and
  an edited dot in the title bar for the document a window is showing; Windows
  conventionally marks the title. An editor wants both.
- **No vibrancy, transparency or window shape**, *absent*. Translucent
  material behind content is how a native macOS sidebar looks, and a
  transparent or shaped window is how anything that is not a rectangle is
  drawn.
- **No online and offline detection**, *absent*. Whether the machine has a
  network, and an event when that changes. React Native's own answer is
  NetInfo, which is a community package rather than core.
- **No spell checking**, *absent* as a service. `spellCheck` on a
  `<TextInput>` is listed under TextInput as an unimplemented prop; the desktop
  version is larger: a dictionary, a language, and a context menu of
  corrections, which is what the platform's own text controls do.
- **No dock, launcher or jump-list menu**, *absent*. The menu a desktop shows
  when you press and hold the icon: recent documents, and the two or three
  actions an app wants offered before it is even running.
- **No way to do work off the JavaScript thread**, *absent*. Electron has
  `utilityProcess` and MessagePorts. Here the equivalent is a worklet runtime or
  a second JSI runtime, and Reanimated already brings one, so this is about
  whether an app can use it for its own work, not about building one.
