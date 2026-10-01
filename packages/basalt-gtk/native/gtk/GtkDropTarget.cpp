#include "GtkDropTarget.h"

#include "DragAndDrop.h"

#include <string>
#include <vector>

namespace basalt {
namespace {

// What the drop target is currently over, so that a leave can be reported for
// the view that was entered rather than for whatever is under the pointer when
// the drag ends -- by then it may be nothing.
facebook::react::Tag gCurrentTarget = 0;

// Turns GTK's dragged value into the shared payload.
//
// Two types are asked for, and both may arrive: G_TYPE_FILE for a file
// manager's drag and G_TYPE_STRING for selected text. A drag carrying neither
// is one this app has nothing to say about, which `wouldAccept` then refuses.
DragPayload payloadFrom(const GValue *value) {
  DragPayload payload;
  if (value == nullptr) {
    return payload;
  }
  if (G_VALUE_HOLDS(value, GDK_TYPE_FILE_LIST)) {
    auto *files = static_cast<GSList *>(g_value_get_boxed(value));
    for (GSList *node = files; node != nullptr; node = node->next) {
      auto *file = static_cast<GFile *>(node->data);
      char *path = file != nullptr ? g_file_get_path(file) : nullptr;
      if (path != nullptr) {
        payload.files.emplace_back(path);
        g_free(path);
      }
    }
  } else if (G_VALUE_HOLDS(value, G_TYPE_FILE)) {
    auto *file = static_cast<GFile *>(g_value_get_object(value));
    char *path = file != nullptr ? g_file_get_path(file) : nullptr;
    if (path != nullptr) {
      payload.files.emplace_back(path);
      g_free(path);
    }
  } else if (G_VALUE_HOLDS(value, G_TYPE_STRING)) {
    const char *text = g_value_get_string(value);
    if (text != nullptr) {
      payload.text = text;
    }
  }
  return payload;
}

// What a drag offers, before it is dropped.
//
// GTK will not hand over the value until the drop on most backends, so the
// question "would anything here take this?" is answered from the formats the
// drag advertises rather than from its contents. That is the same question the
// desktop is asking, which is why the cursor can answer it.
std::uint16_t offeredBy(GdkDrop *drop) {
  std::uint16_t offered = DropAcceptsNone;
  if (drop == nullptr) {
    return offered;
  }
  GdkContentFormats *formats = gdk_drop_get_formats(drop);
  if (formats == nullptr) {
    return offered;
  }
  if (gdk_content_formats_contain_gtype(formats, GDK_TYPE_FILE_LIST) ||
      gdk_content_formats_contain_gtype(formats, G_TYPE_FILE)) {
    offered |= DropAcceptsFiles;
  }
  if (gdk_content_formats_contain_gtype(formats, G_TYPE_STRING)) {
    offered |= DropAcceptsText;
  }
  return offered;
}

void reportLeaveIfAny(double x, double y) {
  if (gCurrentTarget == 0) {
    return;
  }
  DropEvent event;
  event.tag = gCurrentTarget;
  event.phase = DropPhase::Leave;
  event.x = x;
  event.y = y;
  reportDrop(event);
  gCurrentTarget = 0;
}

} // namespace

facebook::react::Tag dropTargetAt(RnView *root, double x, double y, std::uint16_t accepts) {
  if (root == nullptr || !RN_IS_VIEW(root)) {
    return 0;
  }
  GtkWidget *const rootWidget = GTK_WIDGET(root);
  GtkWidget *widget = gtk_widget_pick(rootWidget, x, y, GTK_PICK_DEFAULT);

  for (; widget != nullptr; widget = gtk_widget_get_parent(widget)) {
    // The chain is not all RnViews -- a <TextInput>'s peer is a GtkText -- so
    // anything else is stepped over rather than stopping the walk. Same rule
    // as GtkTitleBarLayout.cpp.
    if (RN_IS_VIEW(widget)) {
      const char *id = rn_view_get_native_id(RN_VIEW(widget));
      if (id != nullptr) {
        const std::uint16_t wanted = dropAcceptsFrom(id);
        if (wanted != DropAcceptsNone && (wanted & accepts) != 0) {
          return rn_view_get_tag(RN_VIEW(widget));
        }
      }
    }
    if (widget == rootWidget) {
      break;
    }
  }
  return 0;
}

DragPayload dragPayloadAt(RnView *root, double x, double y) {
  if (root == nullptr || !RN_IS_VIEW(root)) {
    return {};
  }
  GtkWidget *const rootWidget = GTK_WIDGET(root);
  GtkWidget *widget = gtk_widget_pick(rootWidget, x, y, GTK_PICK_DEFAULT);

  // The same walk the drop side makes, and the same reason: a label inside a
  // draggable view should be draggable without being marked itself.
  for (; widget != nullptr; widget = gtk_widget_get_parent(widget)) {
    if (RN_IS_VIEW(widget)) {
      const char *id = rn_view_get_native_id(RN_VIEW(widget));
      if (id != nullptr) {
        const DragPayload payload = dragPayloadFrom(id);
        if (!payload.empty()) {
          return payload;
        }
      }
    }
    if (widget == rootWidget) {
      break;
    }
  }
  return {};
}

void attachDragSource(RnView *root) {
  if (root == nullptr || !RN_IS_VIEW(root)) {
    return;
  }

  GtkDragSource *source = gtk_drag_source_new();
  gtk_drag_source_set_actions(source, GDK_ACTION_COPY);

  // "prepare" is asked once, when the gesture has gone far enough to be a
  // drag, and its answer *is* the drag: returning NULL means this point is not
  // draggable and the gesture becomes an ordinary press again. Which is why
  // the payload has to be known already -- there is nowhere to wait here.
  g_signal_connect(source,
                   "prepare",
                   G_CALLBACK(+[](GtkDragSource *, double x, double y, gpointer data)
                                  -> GdkContentProvider * {
                     auto *rootView = static_cast<RnView *>(data);
                     const DragPayload payload = dragPayloadAt(rootView, x, y);
                     if (payload.hasFiles()) {
                       GFile *file = g_file_new_for_path(payload.files.front().c_str());
                       GdkContentProvider *provider =
                           gdk_content_provider_new_typed(G_TYPE_FILE, file);
                       g_object_unref(file);
                       return provider;
                     }
                     if (payload.hasText()) {
                       return gdk_content_provider_new_typed(
                           G_TYPE_STRING, payload.text.c_str());
                     }
                     return nullptr;
                   }),
                   root);

  gtk_widget_add_controller(GTK_WIDGET(root), GTK_EVENT_CONTROLLER(source));
}

void attachDropTarget(RnView *root) {
  if (root == nullptr || !RN_IS_VIEW(root)) {
    return;
  }

  // Both types, and COPY only: this platform has no notion of a drag that
  // moves or links, and offering one it cannot honour would be a cursor that
  // promises something the drop does not do.
  GtkDropTarget *target = gtk_drop_target_new(G_TYPE_INVALID, GDK_ACTION_COPY);
  GType types[] = {GDK_TYPE_FILE_LIST, G_TYPE_STRING};
  gtk_drop_target_set_gtypes(target, types, G_N_ELEMENTS(types));

  // "motion" rather than "enter": enter fires once for the *window*, and what
  // an app wants to know is which of its views the pointer is over, which
  // changes without ever leaving.
  g_signal_connect(target,
                   "motion",
                   G_CALLBACK(+[](GtkDropTarget *self, double x, double y, gpointer data)
                                  -> GdkDragAction {
                     auto *rootView = static_cast<RnView *>(data);
                     const std::uint16_t offered = offeredBy(gtk_drop_target_get_current_drop(self));
                     const facebook::react::Tag found = dropTargetAt(rootView, x, y, offered);

                     if (found != gCurrentTarget) {
                       reportLeaveIfAny(x, y);
                     }
                     if (found == 0) {
                       // Nothing here takes it: no action, which is what makes
                       // the cursor say so.
                       return static_cast<GdkDragAction>(0);
                     }
                     gCurrentTarget = found;

                     DropEvent event;
                     event.tag = found;
                     event.phase = DropPhase::Over;
                     event.x = x;
                     event.y = y;
                     reportDrop(event);
                     return GDK_ACTION_COPY;
                   }),
                   root);

  g_signal_connect(target,
                   "leave",
                   G_CALLBACK(+[](GtkDropTarget *, gpointer) { reportLeaveIfAny(0.0, 0.0); }),
                   nullptr);

  g_signal_connect(target,
                   "drop",
                   G_CALLBACK(+[](GtkDropTarget *,
                                  const GValue *value,
                                  double x,
                                  double y,
                                  gpointer data) -> gboolean {
                     auto *rootView = static_cast<RnView *>(data);
                     const DragPayload payload = payloadFrom(value);
                     // Asked again from the payload rather than from the
                     // formats: this is the one moment the contents are known,
                     // and a view that accepts only files should not be handed
                     // a drag that turned out to be only text.
                     std::uint16_t carries = DropAcceptsNone;
                     if (payload.hasFiles()) {
                       carries |= DropAcceptsFiles;
                     }
                     if (payload.hasText()) {
                       carries |= DropAcceptsText;
                     }
                     const facebook::react::Tag found = dropTargetAt(rootView, x, y, carries);
                     gCurrentTarget = 0;
                     if (found == 0) {
                       return FALSE;
                     }

                     DropEvent event;
                     event.tag = found;
                     event.phase = DropPhase::Drop;
                     event.x = x;
                     event.y = y;
                     event.payload = payload;
                     reportDrop(event);
                     return TRUE;
                   }),
                   root);

  gtk_widget_add_controller(GTK_WIDGET(root), GTK_EVENT_CONTROLLER(target));
}

} // namespace basalt
