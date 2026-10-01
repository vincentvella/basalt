#include "GtkTitleBarLayout.h"

#include "TitleBarRegions.h"

namespace basalt {

bool isTitleBarDragRegionAt(RnView *root, double x, double y) {
  if (root == nullptr || !RN_IS_VIEW(root)) {
    return false;
  }

  GtkWidget *const rootWidget = GTK_WIDGET(root);
  // The toolkit's own hit test, in the root's coordinates. NULL when the point
  // is outside the root or lands on nothing targetable, and the loop below
  // then simply does not run -- which is the same answer Windows gives for a
  // miss: not a drag.
  GtkWidget *widget = gtk_widget_pick(rootWidget, x, y, GTK_PICK_DEFAULT);

  for (; widget != nullptr; widget = gtk_widget_get_parent(widget)) {
    // The chain from a picked widget is not all RnViews. A <TextInput>'s peer
    // is a GtkText, a <Switch> is a GtkSwitch, and neither carries a nativeID
    // -- so they are stepped over rather than stopping the walk, and the
    // marked ancestor above them still decides.
    if (RN_IS_VIEW(widget)) {
      const char *id = rn_view_get_native_id(RN_VIEW(widget));
      if (g_strcmp0(id, kTitleBarDragRegionId) == 0) {
        return true;
      }
      if (g_strcmp0(id, kTitleBarNoDragRegionId) == 0) {
        return false;
      }
    }

    // The root is the last thing asked, not the first thing skipped: an app
    // that marks its whole surface as a drag region is answered here. Above it
    // lies the host's own overlay, which is not the app's to mark.
    if (widget == rootWidget) {
      break;
    }
  }

  return false;
}

} // namespace basalt
