// Which parts of an app-drawn header drag the window.
//
// `<TitleBar.DragRegion>` and `<TitleBar.NoDragRegion>` mark a view by setting
// its `nativeID`, because that is the one prop a plain <View> already carries
// all the way to the host on every platform. It is Electron's
// `app-region: drag | no-drag`, spelled in something React Native already has.
//
// The strings live here rather than in a host because every host that reads
// them has to read the *same* ones, and JavaScript writes them from a fourth
// place -- src/TitleBar.tsx, whose comment says they must match. Three copies
// of a string literal is three chances for a header that silently stops
// dragging on one desktop, which is a bug nobody notices until they try it.
//
// The rule each host applies is the same too, and worth stating once: the
// deepest view under the point, then each of its ancestors in turn, and the
// nearest one marked either way decides. So a label inside a drag region drags
// without being marked, a button inside one stays pressable once it is marked
// no-drag, and a point under nothing marked is ordinary client area.
//
// The hit test itself is per-host -- Win32 walks its own view tree, GTK asks
// the toolkit with gtk_widget_pick -- because the tree is the toolkit's, not
// this project's. Only the vocabulary is shared.

#pragma once

namespace basalt {

inline constexpr const char *kTitleBarDragRegionId = "basalt-titlebar-drag";
inline constexpr const char *kTitleBarNoDragRegionId = "basalt-titlebar-no-drag";

} // namespace basalt
