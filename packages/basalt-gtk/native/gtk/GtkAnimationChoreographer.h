// Drives React Native's animation frame callbacks from GTK's frame clock.
//
// AnimationChoreographer is the frame-callback seam in ReactCxxPlatform. It is
// NOT what drives mounting -- mounts arrive on the JS thread via the
// RuntimeScheduler and are marshalled by GtkMountingManager. This is purely
// the "there is a new frame" signal the animation backend needs.

#pragma once

#include <gtk/gtk.h>

#include "DisplayChoreographer.h"

namespace basalt {

class GtkAnimationChoreographer final : public DisplayChoreographer {
 public:
  GtkAnimationChoreographer() = default;
  ~GtkAnimationChoreographer() override;


  // Called on the GTK main thread once the surface root is realised: a widget
  // has no frame clock before realisation.
  void attachToWidget(GtkWidget *widget);
  void detach();

 protected:
  // Marshals to the main thread before touching the frame clock.
  void runningChanged() override;

 private:
  static void onFrameClockUpdate(GdkFrameClock *clock, gpointer userData);
  static gboolean syncRunningStateOnMainThread(gpointer userData);

  // gdk_frame_clock_begin_updating/end_updating are refcounted, so calls must
  // be balanced. This tracks whether our single reference is currently held.
  void setUpdating(bool shouldUpdate);

  GdkFrameClock *frameClock_{nullptr};
  gulong updateHandler_{0};
  bool updating_{false};

};

} // namespace basalt
