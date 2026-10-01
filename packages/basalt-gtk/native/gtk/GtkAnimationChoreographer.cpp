#include "GtkAnimationChoreographer.h"

namespace basalt {

GtkAnimationChoreographer::~GtkAnimationChoreographer() {
  detach();
}

void GtkAnimationChoreographer::onFrameClockUpdate(GdkFrameClock * /*clock*/, gpointer userData) {
  auto *self = static_cast<GtkAnimationChoreographer *>(userData);
  self->onAnimationFrame(self->now());
}

gboolean GtkAnimationChoreographer::syncRunningStateOnMainThread(gpointer userData) {
  auto *self = static_cast<GtkAnimationChoreographer *>(userData);
  self->setUpdating(self->running_.load(std::memory_order_acquire));
  return G_SOURCE_REMOVE;
}

void GtkAnimationChoreographer::setUpdating(bool shouldUpdate) {
  if (frameClock_ == nullptr || shouldUpdate == updating_) {
    return;
  }
  if (shouldUpdate) {
    gdk_frame_clock_begin_updating(frameClock_);
  } else {
    gdk_frame_clock_end_updating(frameClock_);
  }
  updating_ = shouldUpdate;
}

void GtkAnimationChoreographer::attachToWidget(GtkWidget *widget) {
  detach();

  GdkFrameClock *clock = gtk_widget_get_frame_clock(widget);
  if (clock == nullptr) {
    g_warning("GtkAnimationChoreographer: widget has no frame clock (not "
              "realised yet); animations will not tick");
    return;
  }

  frameClock_ = GDK_FRAME_CLOCK(g_object_ref(clock));
  updateHandler_ = g_signal_connect(frameClock_, "update",
                                    G_CALLBACK(onFrameClockUpdate), this);

  // A resume() may have arrived before the widget was realised.
  setUpdating(running_.load(std::memory_order_acquire));
}

void GtkAnimationChoreographer::detach() {
  if (frameClock_ == nullptr) {
    return;
  }
  setUpdating(false);
  if (updateHandler_ != 0) {
    g_signal_handler_disconnect(frameClock_, updateHandler_);
    updateHandler_ = 0;
  }
  g_object_unref(frameClock_);
  frameClock_ = nullptr;
}

void GtkAnimationChoreographer::resume() {
  running_.store(true, std::memory_order_release);
  g_idle_add_full(G_PRIORITY_DEFAULT, syncRunningStateOnMainThread, this, nullptr);
}

void GtkAnimationChoreographer::pause() {
  running_.store(false, std::memory_order_release);
  g_idle_add_full(G_PRIORITY_DEFAULT, syncRunningStateOnMainThread, this, nullptr);
}

} // namespace basalt
