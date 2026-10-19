// Whether a point in an app-drawn header drags the window.
//
// The geometry half of AppKitTitleBar, kept apart from it for the reason
// Win32TitleBarLayout and GtkTitleBarLayout are kept apart from theirs: it
// needs a view tree and nothing else -- no window, no React Native -- so it
// can be tested with neither. What a custom caption gets wrong is exactly
// this: a label that will not drag, a button that drags instead of pressing.
//
// There is no caption-button arithmetic here, as there is on Windows. macOS
// keeps drawing the traffic lights over the content view under
// NSWindowStyleMaskFullSizeContentView, so they hit-test themselves, and
// AppKitTitleBar::metrics() reports where they are. Only the drag question is
// left, which is the same one GTK is left with and for the same reason.

#pragma once

#import "RnAppKitView.h"

namespace basalt {

// Whether a point, in the root's coordinates, drags the window.
//
// The rule is core/TitleBarRegions.h's, and it is the one the other two hosts
// apply: the deepest view under the point and then each ancestor in turn, with
// the nearest one marked either way deciding. A label inside a drag region
// drags without being marked, a button inside one stays pressable once it is
// marked no-drag, and a point under nothing marked is ordinary client area.
//
// `RnAppKitHitTest` rather than NSView's `hitTest:`, for the reason
// AppKitDropTarget.mm gives at length: the platform's own is what touches use,
// so the view this finds is the view a click would land on. The walk up from
// there is ours, because a label inside a header should not need marking.
bool isTitleBarDragRegionAt(RnAppKitView *root, double x, double y);

} // namespace basalt
