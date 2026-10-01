// Whether a point in an app-drawn header drags the window.
//
// The geometry half of GtkTitleBar, kept apart from it for the same reason
// Win32TitleBarLayout is kept apart from Win32TitleBar: it needs a view tree
// and nothing else -- no window, no toplevel, no React Native -- so it can be
// tested with none of them. What a custom caption gets wrong is exactly this
// -- a label that will not drag, a button that drags instead of pressing --
// and it is invisible to every other test.
//
// There is no GTK equivalent of the caption-button arithmetic that fills out
// the Windows file. GTK takes the whole titlebar away with the decorations and
// the host puts a real GtkWindowControls back, so the buttons are widgets that
// place and hit-test themselves. Only the drag question is left.

#pragma once

#include "RnView.h"

namespace basalt {

// Whether a point, in the root's coordinates, drags the window.
//
// The rule is core/TitleBarRegions.h's, and it is the one Win32 applies: the
// deepest view under the point and then each ancestor in turn, with the
// nearest one marked either way deciding. A label inside a drag region drags
// without being marked, a button inside one stays pressable once it is marked
// no-drag, and a point under nothing marked is ordinary client area.
//
// Unlike the Windows host this does not walk the tree itself -- gtk_widget_pick
// is the toolkit's own answer to "what is under this point", and it already
// honours can-target, so a view with pointerEvents="none" is skipped here for
// the same reason a click would miss it. Writing a second walk would be a
// second chance to disagree with the toolkit about what was clicked.
bool isTitleBarDragRegionAt(RnView *root, double x, double y);

} // namespace basalt
