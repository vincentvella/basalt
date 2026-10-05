# Drag and drop, in and out of the application

## Why

A desktop application is expected to accept a file dropped on it and to let its
own content be dragged elsewhere. React Native has no API for either, because a
phone has no pointer to drag with, and this platform has implemented neither --
so an app here cannot take a file from the file manager, and nothing it displays
can be dragged out.

## What Changes

- A drop target seam: a view can declare what it accepts and be told when
  something is dragged over it, dropped on it, or leaves it.
- A drag source seam: a view can declare what it represents and begin a drag.
- Hit testing against the drag position, so the innermost accepting view under
  the pointer is the one told.
- Payload types covering at least files and plain text, which are the two every
  desktop agrees on.

## Capabilities

### New Capabilities
- `drag-and-drop`

### Modified Capabilities
- None. `pointer-input` describes pressing and hovering; a drag is its own
  protocol on every one of these toolkits rather than a gesture built on those.

## Impact

- `core/PlatformServices.h` gains a drag seam, implemented three times:
  `GtkDropTarget` and `GdkContentProvider`, `NSDraggingDestination` and
  `NSPasteboardWriting`, and OLE's `IDropTarget` with `DoDragDrop`.
- The mounting managers gain per-view drop registration, alongside the hover
  listener bookkeeping they already keep.
- `e2e/` gains a demo, and `scripts/integration_test.py` a scenario; a synthetic
  drag will need a test instrument, as taps and hovers did.
