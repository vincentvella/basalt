// The GTK half of drag and drop: what happens when something is dragged onto
// the window.
//
// See core/DragAndDrop.h for the model, which is shared, and for why a view
// declares itself a drop target by its nativeID.

#pragma once

#include "DragAndDrop.h"
#include "RnView.h"

#include <gtk/gtk.h>

#include <cstdint>

#include <react/renderer/core/ReactPrimitives.h>

namespace basalt {

// The deepest view under this point that would take what is being dragged, or
// 0 for none.
//
// The same walk `isTitleBarDragRegionAt` makes, and it has to be: a label
// inside a drop target should not need marking, and a drop target inside
// another should win.
facebook::react::Tag dropTargetAt(RnView *root, double x, double y, std::uint16_t accepts);

// Attaches a GtkDropTarget to the surface root, so the window accepts files
// and text dragged onto it from anywhere on the desktop.
void attachDropTarget(RnView *root);

// The payload a marked view under this point represents, or an empty one.
// The other direction: see core/DragAndDrop.h.
DragPayload dragPayloadAt(RnView *root, double x, double y);

// Attaches a GtkDragSource, so a marked view can be dragged out of the window
// to anywhere on the desktop.
void attachDragSource(RnView *root);

} // namespace basalt
