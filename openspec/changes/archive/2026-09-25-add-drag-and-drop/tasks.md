# Tasks

## 1. The seam

- [x] Define the payload and drop-target types in `core/` as a header-only model
- [x] ~~Add the drop registration to `MountingWalk`, beside the hover
      listeners.~~ Not needed, and the change is better for it: a view marks
      itself with `nativeID`, which every host already stores and React
      already sends on every commit. No registration, no ref, no lifetime.
      Same idea as `core/TitleBarRegions.h`
- [x] Add hit testing for a drag position, reusing each host's own hit test
      -- `RnAppKitHitTest`, `gtk_widget_pick`, `hitTest` -- and then walking
      up to the nearest marked ancestor

## 2. GTK

Both directions are implemented. Dragging *out* is the harder one and the one
no test can drive: the system owns the drag once it starts, and no instrument
can put a file manager on the other end. Its verification is the demo's green
row and a person dragging it somewhere, which is stated in the demo and in
docs/TESTING.md rather than left for somebody to discover.


- [x] Attach a `GtkDropTarget` to the surface root and route its signals
- [x] Implement dragging out with a `GdkContentProvider`, from a
      `GtkDragSource` whose `prepare` signal answers with the payload

## 3. AppKit

- [x] Implement `NSDraggingDestination` -- on a view of its own above the
      surface root, not on `RnAppKitView`. Every view would otherwise be a
      destination and AppKit would ask the deepest, which is the opposite of
      the rule: the deepest *marked* view decides
- [x] Implement dragging out with `NSPasteboardWriting` -- an `NSURL` for a
      file, an `NSString` for text -- begun from the touch dispatcher's
      `mouseDragged`, with a separate `NSDraggingSource` object because a view
      is a drag source only while a drag it started is running

## 4. Win32

- [x] Register an `IDropTarget` per window and route its methods, with
      `OleInitialize` in place of `CoInitializeEx` -- a thread that only called
      the latter can open a URL and cannot accept a drop
- [x] Implement dragging out with `DoDragDrop`, which needed the most of the
      three by far: OLE has no simple data object, so one thing being dragged
      means a hand-written `IDataObject`, an `IDropSource`, and
      `SHCreateStdEnumFmtEtc` to advertise the single format. A file goes as
      `CF_HDROP` with a `DROPFILES` header, which is what makes another
      application receive a file rather than a string that looks like one

## 5. JavaScript and tests

- [x] Expose the React API -- `<DropTarget>` -- and export it from
      `basalt-core`
- [x] Add a demo to `js/drop.js`: two targets, one inside the other, because
      the rule that can be wrong is which one is told
- [x] Add `BASALT_TEST_DROP` on both hosts, and a scenario asserting the
      innermost target is told and the outer one is not. Entered below the
      toolkit, like taps: a real drag needs a source outside the process
- [x] Update `docs/TESTING.md` and `docs/backlog/desktop-capabilities.md`


## 6. What building it found

- [x] AppKit's `hitTest:` is not this platform's hit test. It answers with the
      first subview that claims the point rather than the one the renderer
      means, so the walk started two steps below the root and found no marker
      above it. `RnAppKitHitTest` -- what touches use -- is the right one.
- [x] `<DropTarget>` never touched the native module, so `BasaltWindows` was
      never constructed, its constructor never registered the host listener,
      and every drop was found in C++ and reported into a null std::function.
      The log said "onto tag 18" and JavaScript heard nothing.
- [x] A host component's ref carries `__nativeTag`, not `_nativeTag`. The old
      name is simply undefined under the new renderer, and an event that
      matches nothing looks exactly like an event for another view.
- [x] AppKit stored no `nativeID` at all. GTK and Win32 did; nothing on that
      host had needed it.

## 7. What dragging out needed that dropping in did not

- [x] The payload declared in advance rather than asked for. Every toolkit
      owns the gesture -- `GtkDragSource`,
      `beginDraggingSessionWithItems:` and `DoDragDrop` all start the drag and
      ask what is being dragged synchronously, on the UI thread. An app asked
      then would have to answer from JavaScript on another thread, which is
      the problem `useCloseRequest` has and is solved the same way.
- [x] A per-press "already asked" flag on two hosts. A press is not a drag
      until it moves, so the question is asked on the first move rather than
      on the way down -- and exactly once, because the modal drag loop means
      the touch that began it never ends and has to be cancelled.
- [x] No automated coverage, said plainly. The gesture tests passing proves
      only that a drag source is inert when nothing is marked, which is worth
      knowing and is not the same as proving a drag works.
