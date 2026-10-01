// The Win32 half of drag and drop: what happens when something is dragged onto
// the window.
//
// See core/DragAndDrop.h for the model, which is shared, and for why a view
// declares itself a drop target by its nativeID.

#pragma once

#include "DragAndDrop.h"
#include "RnWin32View.h"

#include <windows.h>

#include <cstdint>

#include <react/renderer/core/ReactPrimitives.h>

namespace basalt {

// The deepest view under this point that would take what is being dragged, or
// 0 for none. The point is in the root's coordinates.
facebook::react::Tag dropTargetAt(win32::RnWin32View *root,
                                  double x,
                                  double y,
                                  std::uint16_t accepts);

// Registers an OLE drop target for this window, so it accepts files and text
// dragged onto it. Balanced by `detachDropTarget` at teardown: a window that
// is destroyed while registered leaves OLE holding a pointer to it.
void attachDropTarget(HWND window, win32::RnWin32View *root);
void detachDropTarget(HWND window);

// The payload a marked view under this point represents, or an empty one.
DragPayload dragPayloadAt(win32::RnWin32View *root, double x, double y);

// Begins a drag out of the window if the press began on a marked view. True
// when it did -- and DoDragDrop runs its own modal loop until the drop, so a
// caller that hears yes has already lost the gesture and should cancel the
// touch it was tracking.
bool beginDragIfMarked(win32::RnWin32View *root, double x, double y);

} // namespace basalt
