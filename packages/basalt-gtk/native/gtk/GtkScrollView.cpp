#include "GtkScrollView.h"

#include <react/renderer/components/scrollview/ScrollEvent.h>
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/core/ConcreteState.h>

#include <algorithm>
#include <cmath>

namespace basalt {

using facebook::react::EdgeInsets;
using facebook::react::Float;
using facebook::react::Point;
using facebook::react::ScrollEvent;
using facebook::react::ScrollEndDragEvent;
using facebook::react::ScrollViewEventEmitter;
using facebook::react::ScrollViewProps;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ShadowView;
using facebook::react::Size;
using facebook::react::Tag;

namespace {

// One axis of the two inset props, in the order core/ScrollBounds.h wants them.
ScrollAxisInsets verticalInsets(const facebook::react::EdgeInsets &insets) {
  return {insets.top, insets.bottom};
}

ScrollAxisInsets horizontalInsets(const facebook::react::EdgeInsets &insets) {
  return {insets.left, insets.right};
}

} // namespace

GtkScrollViewManager::GtkScrollViewManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {}

// ---------------------------------------------------------------------------
// Mutation handling
// ---------------------------------------------------------------------------

void GtkScrollViewManager::update(RnView *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;
  entry.owner = this;

  // A ScrollView is the one view that must clip: its content is deliberately
  // larger than its frame, and without this it paints over its siblings.
  rn_view_set_clips_children(view, TRUE);

  entry.containerSize = shadowView.layoutMetrics.frame.size;

  if (const auto props = std::dynamic_pointer_cast<const ScrollViewProps>(shadowView.props)) {
    entry.scrollEnabled = props->scrollEnabled;
    entry.showsVerticalIndicator = props->showsVerticalScrollIndicator;
    entry.showsHorizontalIndicator = props->showsHorizontalScrollIndicator;
    entry.contentInset = props->contentInset;
    entry.indicatorInset = props->scrollIndicatorInsets;
    entry.eventThrottleMs = static_cast<double>(props->scrollEventThrottle);
    // React Native resolves 'normal' and 'fast' to numbers before this sees
    // them; zero is what an app that never set the prop leaves behind, and
    // means the default rather than a fling that stops instantly.
    if (props->decelerationRate > 0) {
      entry.decelerationRate = static_cast<double>(props->decelerationRate);
    }

    entry.snap.paging = props->pagingEnabled;
    entry.snap.interval = static_cast<double>(props->snapToInterval);
    entry.snap.offsets.clear();
    for (const auto offset : props->snapToOffsets) {
      entry.snap.offsets.push_back(static_cast<double>(offset));
    }
    switch (props->snapToAlignment) {
      case facebook::react::ScrollViewSnapToAlignment::Center:
        entry.snap.alignment = ScrollSnapAlignment::Center;
        break;
      case facebook::react::ScrollViewSnapToAlignment::End:
        entry.snap.alignment = ScrollSnapAlignment::End;
        break;
      case facebook::react::ScrollViewSnapToAlignment::Start:
      default:
        entry.snap.alignment = ScrollSnapAlignment::Start;
        break;
    }
  }

  if (const auto state =
          std::dynamic_pointer_cast<const ScrollViewShadowNode::ConcreteState>(shadowView.state)) {
    entry.state = state;
    const auto &data = state->getData();
    entry.contentSize = data.getContentSize();

    if (inserted) {
      // First sight of this ScrollView. The state may already carry an offset,
      // from the `contentOffset` prop or from a surface that was suspended and
      // remounted, so adopt it rather than starting at zero.
      entry.offsetX = data.contentOffset.x;
      entry.offsetY = data.contentOffset.y;
    }
  }

  // Re-clamp: the content may have shrunk under a scrolled offset.
  const double x = clampScrollOffset(entry.offsetX,
                                     entry.containerSize.width,
                                     entry.contentSize.width,
                                     horizontalInsets(entry.contentInset));
  const double y = clampScrollOffset(entry.offsetY,
                                     entry.containerSize.height,
                                     entry.contentSize.height,
                                     verticalInsets(entry.contentInset));
  entry.offsetX = x;
  entry.offsetY = y;
  rn_view_set_scroll_offset(view, x, y);
  // Here as well as in applyOffset: a list that grew or a window that was
  // resized changes the thumb without changing the offset at all.
  updateIndicators(entry);

  if (inserted) {
    // GTK_EVENT_CONTROLLER_SCROLL_KINETIC asks GDK for one `decelerate` signal
    // carrying the velocity a gesture ended at. It is all GTK offers: the
    // coasting itself is this file's, through core/ScrollMomentum.h, which is
    // the half macOS gets from the system.
    entry.controller = gtk_event_controller_scroll_new(
        static_cast<GtkEventControllerScrollFlags>(GTK_EVENT_CONTROLLER_SCROLL_BOTH_AXES |
                                                   GTK_EVENT_CONTROLLER_SCROLL_KINETIC));
    g_signal_connect(entry.controller, "scroll", G_CALLBACK(onScroll), &entry);
    g_signal_connect(entry.controller, "scroll-begin", G_CALLBACK(onScrollBegin), &entry);
    g_signal_connect(entry.controller, "scroll-end", G_CALLBACK(onScrollEnd), &entry);
    g_signal_connect(entry.controller, "decelerate", G_CALLBACK(onDecelerate), &entry);
    gtk_widget_add_controller(GTK_WIDGET(view), entry.controller);
  }
}

void GtkScrollViewManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // Detach before the Entry goes away: the controller's callbacks hold a
  // pointer to it, and gtk_widget_add_controller means the widget, not this,
  // owns the controller's lifetime.
  Entry &entry = it->second;
  // The tick callbacks hold the same pointer the controller does, and outlive
  // neither. Stopped without an event: the emitter is going away with the view.
  stopMomentum(entry, false);
  stopAnimation(entry);
  if (entry.controller != nullptr && entry.view != nullptr && RN_IS_VIEW(entry.view)) {
    gtk_widget_remove_controller(GTK_WIDGET(entry.view), entry.controller);
  }
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// GTK callbacks
// ---------------------------------------------------------------------------

gboolean GtkScrollViewManager::onScroll(GtkEventControllerScroll *controller,
                                        double dx,
                                        double dy,
                                        gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (!entry->scrollEnabled) {
    return GDK_EVENT_PROPAGATE;
  }
  // A wheel arrives without a `scroll-begin`, so the cancellation there does not
  // cover it.
  entry->owner->stopAnimation(*entry);

  // A wheel reports discrete notches, a touchpad reports pixels. Treating the
  // first as pixels makes the wheel move the content by one pixel a click.
  double stepX = dx;
  double stepY = dy;
  if (gtk_event_controller_scroll_get_unit(controller) == GDK_SCROLL_UNIT_WHEEL) {
    stepX *= GtkScrollViewManager::kWheelStepPixels;
    stepY *= GtkScrollViewManager::kWheelStepPixels;
  }

  entry->owner->scrollEntry(*entry, stepX, stepY);
  return GDK_EVENT_STOP;
}

void GtkScrollViewManager::scrollEntry(Entry &entry, double dx, double dy) {
  // The pull. A desktop scroll view has no rubber band to stretch, so what a
  // <RefreshControl> gets instead is the wheel still asking to go up after the
  // offset has already reached zero. Reported before the offset is applied,
  // because applying it changes nothing at the top and there would be nothing
  // left to see.
  if (overscrollTop_) {
    const bool pastTop = dy < 0.0 && entry.offsetY <= 0.0;
    overscrollTop_(entry.tag, pastTop ? -dy : 0.0);
  }

  applyOffset(entry, entry.offsetX + dx, entry.offsetY + dy, true);
}

bool GtkScrollViewManager::scrollAt(RnView *root, double x, double y, double dx, double dy) {
  if (root == nullptr) {
    return false;
  }
  GtkWidget *picked = gtk_widget_pick(
      GTK_WIDGET(root), x, y, GTK_PICK_DEFAULT);
  // Up from whatever was hit to the nearest view this manager knows about,
  // which is what a wheel does anyway: a plain view inside a list scrolls the
  // list, and a list inside a list scrolls the inner one.
  for (GtkWidget *walk = picked; walk != nullptr; walk = gtk_widget_get_parent(walk)) {
    if (!RN_IS_VIEW(walk)) {
      continue;
    }
    const auto found = entries_.find(static_cast<facebook::react::Tag>(rn_view_get_tag(RN_VIEW(walk))));
    if (found == entries_.end()) {
      continue;
    }
    if (!found->second.scrollEnabled) {
      return false;
    }
    scrollEntry(found->second, dx, dy);
    return true;
  }
  return false;
}

void GtkScrollViewManager::onScrollBegin(GtkEventControllerScroll * /*controller*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (!entry->scrollEnabled) {
    return;
  }
  // A new gesture takes the list off whatever it was coasting towards, which is
  // what putting a finger on a moving list does everywhere else -- and off an
  // animated `scrollTo` too, which is the same expectation: the person moving
  // the list wins over the app moving it.
  entry->owner->stopMomentum(*entry, true);
  entry->owner->stopAnimation(*entry);
  entry->dragging = true;
  entry->owner->emitScrollEvent(*entry, "beginDrag");
}

void GtkScrollViewManager::onScrollEnd(GtkEventControllerScroll * /*controller*/, gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  if (!entry->dragging) {
    return;
  }
  entry->dragging = false;
  entry->owner->emitScrollEvent(*entry, "endDrag");
  // A release with no velocity. `decelerate` arrives separately when there was
  // a flick, and settles with the velocity it carries; this is the other case.
  entry->owner->settleOnSnapPoint(*entry, 0.0);
}

bool GtkScrollViewManager::settleOnSnapPoint(Entry &entry, double velocityY) {
  const auto target = scrollSnapTarget(entry.snap,
                                       entry.offsetY,
                                       velocityY,
                                       entry.containerSize.height,
                                       entry.contentSize.height);
  if (!target) {
    return false;
  }
  // Animated, which is what makes it a snap rather than a jump -- and the
  // reason `core/ScrollAnimation.h` came first.
  scrollTowards(entry, entry.offsetX, *target, true);
  return true;
}

// ---------------------------------------------------------------------------
// Momentum
// ---------------------------------------------------------------------------

void GtkScrollViewManager::onDecelerate(GtkEventControllerScroll * /*controller*/,
                                        double velocityX,
                                        double velocityY,
                                        gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  entry->owner->fling(entry->tag, velocityX, velocityY);
}

bool GtkScrollViewManager::fling(Tag tag, double velocityX, double velocityY) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;
  if (!entry.scrollEnabled || entry.view == nullptr) {
    return false;
  }

  // GTK emits `decelerate` around the same moment as `scroll-end`, and does not
  // promise which comes first. React Native's order does promise: a drag ends
  // before the momentum after it begins, and a JavaScript list that sees them
  // the other way round decides a scroll is still in progress after it has
  // finished. So the drag is ended here if it has not been already.
  if (entry.dragging) {
    entry.dragging = false;
    emitScrollEvent(entry, "endDrag");
  }

  // A snapping list does not coast. React Native replaces the fling with a
  // settle, which is what makes a page turn one page rather than however many
  // the throw was worth -- the velocity decides *which* point rather than how
  // far.
  //
  // Here rather than in the `decelerate` handler so that the test seam below
  // reaches it: a real fling needs a touchscreen, and `fling()` is how the unit
  // suite gets one.
  if (settleOnSnapPoint(entry, velocityY)) {
    return false;
  }

  if (!entry.momentum.start(velocityX, velocityY, entry.decelerationRate)) {
    return false;
  }
  entry.momentumLastMicros = g_get_monotonic_time();
  emitScrollEvent(entry, "momentumBegin");
  entry.momentumTickId =
      gtk_widget_add_tick_callback(GTK_WIDGET(entry.view), onMomentumTick, &entry, nullptr);
  return true;
}

bool GtkScrollViewManager::advanceFling(Tag tag, double seconds) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;
  if (!entry.momentum.isRunning()) {
    return false;
  }

  double dx = 0;
  double dy = 0;
  const bool running = entry.momentum.advance(seconds, dx, dy);

  const double beforeX = entry.offsetX;
  const double beforeY = entry.offsetY;
  applyOffset(entry, beforeX + dx, beforeY + dy, true);

  // Stopped, or run into an edge and gone nowhere. The second is not the same
  // check as the first: a fling at the top of a list has velocity left and
  // nothing to spend it on, and without this it would coast silently for a
  // second before reporting that it had finished.
  const bool moved = entry.offsetX != beforeX || entry.offsetY != beforeY;
  if (running && moved) {
    return true;
  }
  entry.momentum.stop();
  if (entry.momentumTickId != 0) {
    // Cleared rather than removed: a tick callback that returns G_SOURCE_REMOVE
    // is already gone, and removing it again is a GTK warning.
    entry.momentumTickId = 0;
  }
  emitScrollEvent(entry, "momentumEnd");
  return false;
}

gboolean GtkScrollViewManager::onAnimationTick(GtkWidget * /*widget*/,
                                               GdkFrameClock *clock,
                                               gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);
  // The frame clock's time, for the same reason the fling uses it: it is the
  // time the frame is *for*, so a late callback does not shorten the curve.
  const gint64 now = gdk_frame_clock_get_frame_time(clock);
  const double seconds = static_cast<double>(now - entry->animationLastMicros) / 1e6;
  entry->animationLastMicros = now;

  return entry->owner->advanceAnimation(entry->tag, seconds) ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

void GtkScrollViewManager::scrollTowards(Entry &entry, double x, double y, bool animated) {
  stopAnimation(entry);
  if (!animated || !entry.animation.start(entry.offsetX, entry.offsetY, x, y)) {
    // Not animated, or already there. Either way the offset is the answer and
    // there is no curve to run.
    applyOffset(entry, x, y, true);
    return;
  }
  entry.animationLastMicros = g_get_monotonic_time();
  entry.animationTickId =
      gtk_widget_add_tick_callback(GTK_WIDGET(entry.view), onAnimationTick, &entry, nullptr);
}

bool GtkScrollViewManager::advanceAnimation(Tag tag, double seconds) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;
  if (!entry.animation.isRunning()) {
    return false;
  }

  double x = entry.offsetX;
  double y = entry.offsetY;
  const bool running = entry.animation.advance(seconds, x, y);
  applyOffset(entry, x, y, true);

  if (!running) {
    entry.animationTickId = 0;
  }
  return running;
}

void GtkScrollViewManager::stopAnimation(Entry &entry) {
  entry.animation.stop();
  if (entry.animationTickId != 0) {
    gtk_widget_remove_tick_callback(GTK_WIDGET(entry.view), entry.animationTickId);
    entry.animationTickId = 0;
  }
}

gboolean GtkScrollViewManager::onMomentumTick(GtkWidget * /*widget*/,
                                              GdkFrameClock *clock,
                                              gpointer userData) {
  auto *entry = static_cast<Entry *>(userData);

  // The frame clock's own time rather than g_get_monotonic_time: it is the time
  // the frame is *for*, so a fling advances by exactly one frame's worth even
  // when the callback runs late.
  const gint64 now = gdk_frame_clock_get_frame_time(clock);
  const double seconds = static_cast<double>(now - entry->momentumLastMicros) / 1e6;
  entry->momentumLastMicros = now;

  return entry->owner->advanceFling(entry->tag, seconds) ? G_SOURCE_CONTINUE : G_SOURCE_REMOVE;
}

void GtkScrollViewManager::stopMomentum(Entry &entry, bool emitEnd) {
  if (entry.momentumTickId != 0) {
    if (entry.view != nullptr && RN_IS_VIEW(entry.view)) {
      gtk_widget_remove_tick_callback(GTK_WIDGET(entry.view), entry.momentumTickId);
    }
    entry.momentumTickId = 0;
    if (emitEnd) {
      emitScrollEvent(entry, "momentumEnd");
    }
  }
  entry.momentum.stop();
}

// ---------------------------------------------------------------------------
// Offset, events and state
// ---------------------------------------------------------------------------

void GtkScrollViewManager::applyOffset(Entry &entry, double x, double y, bool emitEvent) {
  const double clampedX = clampScrollOffset(x,
                                            entry.containerSize.width,
                                            entry.contentSize.width,
                                            horizontalInsets(entry.contentInset));
  const double clampedY = clampScrollOffset(y,
                                            entry.containerSize.height,
                                            entry.contentSize.height,
                                            verticalInsets(entry.contentInset));

  if (clampedX == entry.offsetX && clampedY == entry.offsetY) {
    return;
  }
  entry.offsetX = clampedX;
  entry.offsetY = clampedY;

  rn_view_set_scroll_offset(entry.view, clampedX, clampedY);
  updateIndicators(entry);

  // Unthrottled, and separate from onScroll on purpose.
  // ScrollViewShadowNode::getContentOriginOffset reads this, and through it so
  // do measure, measureLayout, C++ hit testing and view culling. Throttling it
  // would leave all of those reporting a stale scroll position.
  writeStateOffset(entry);

  if (!emitEvent) {
    return;
  }

  // scrollEventThrottle is the platform's job. Zero means every scroll.
  const gint64 now = g_get_monotonic_time();
  if (entry.eventThrottleMs > 0) {
    const gint64 interval = static_cast<gint64>(entry.eventThrottleMs * 1000.0);
    if (now - entry.lastEmitMicros < interval) {
      return;
    }
  }
  entry.lastEmitMicros = now;
  emitScrollEvent(entry, "scroll");
}

// The overlay scrollbars. The geometry is core/ScrollIndicator.h's, so GTK,
// AppKit and Win32 place the same thumb in the same place.
//
// Always drawn, rather than faded in while scrolling and out after: there is no
// timer here and no animation, which is why `flashScrollIndicators` stays a
// no-op -- there is nothing to flash something already on screen.
void GtkScrollViewManager::updateIndicators(const Entry &entry) {
  if (entry.view == nullptr) {
    return;
  }

  const ScrollIndicator vertical = entry.showsVerticalIndicator
                                       ? scrollIndicatorFor(entry.containerSize.height,
                                                            entry.contentSize.height,
                                                            entry.offsetY,
                                                            verticalInsets(entry.contentInset),
                                                            verticalInsets(entry.indicatorInset))
                                       : ScrollIndicator{};
  const ScrollIndicator horizontal =
      entry.showsHorizontalIndicator ? scrollIndicatorFor(entry.containerSize.width,
                                                          entry.contentSize.width,
                                                          entry.offsetX,
                                                          horizontalInsets(entry.contentInset),
                                                          horizontalInsets(entry.indicatorInset))
                                     : ScrollIndicator{};

  rn_view_set_scroll_indicators(
      entry.view, vertical.offset, vertical.length, horizontal.offset, horizontal.length);
}

void GtkScrollViewManager::emitScrollEvent(Entry &entry, const char *which) {
  const auto emitter = std::dynamic_pointer_cast<const ScrollViewEventEmitter>(lookup_(entry.tag));
  if (emitter == nullptr) {
    return;
  }

  const auto fill = [&entry](ScrollEvent &event) {
    event.contentOffset = Point{.x = static_cast<Float>(entry.offsetX), .y = static_cast<Float>(entry.offsetY)};
    event.contentSize = entry.contentSize;
    event.containerSize = entry.containerSize;
    event.contentInset = entry.contentInset;
    // Not a default worth trusting: ScrollEvent initialises zoomScale to 0, and
    // VirtualizedList only repairs negative values, so leaving it would make
    // every list measurement come out as zero.
    event.zoomScale = 1.0F;
    event.timestamp = static_cast<Float>(g_get_monotonic_time()) / 1e6F;
  };

  if (std::string_view(which) == "endDrag") {
    ScrollEndDragEvent event{};
    fill(event);
    // No velocity model yet, so a drag ends where it stopped. Without a
    // targetContentOffset the JS side treats the drag as ending immediately,
    // which is what actually happens here.
    event.targetContentOffset = event.contentOffset;
    event.velocity = Point{.x = 0, .y = 0};
    emitter->onScrollEndDrag(event);
    return;
  }

  ScrollEvent event{};
  fill(event);
  const std::string_view kind(which);
  if (kind == "beginDrag") {
    emitter->onScrollBeginDrag(event);
  } else if (kind == "momentumBegin") {
    emitter->onMomentumScrollBegin(event);
  } else if (kind == "momentumEnd") {
    emitter->onMomentumScrollEnd(event);
  } else {
    emitter->onScroll(event);
  }
}

void GtkScrollViewManager::writeStateOffset(const Entry &entry) {
  if (entry.state == nullptr) {
    return;
  }
  const Point offset{.x = static_cast<Float>(entry.offsetX), .y = static_cast<Float>(entry.offsetY)};

  entry.state->updateState(
      [offset](const ScrollViewShadowNode::ConcreteState::Data &oldData)
          -> ScrollViewShadowNode::ConcreteState::SharedData {
        if (oldData.contentOffset == offset) {
          // Returning null cancels the update. The callback can run more than
          // once when commits race, so it must stay a pure function of oldData.
          return nullptr;
        }
        auto newData = oldData;
        newData.contentOffset = offset;
        return std::make_shared<const ScrollViewShadowNode::ConcreteState::Data>(newData);
      });
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

bool GtkScrollViewManager::dispatchCommand(Tag tag, const std::string &name, const folly::dynamic &args) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;
  // A programmatic scroll wins over a fling, which is what an app calling
  // scrollTo means by it. Told to JavaScript, because the fling was announced.
  if (name == "scrollTo" || name == "scrollToEnd") {
    stopMomentum(entry, true);
  }

  // [x, y, animated] for scrollTo, [animated] for scrollToEnd.
  if (name == "scrollTo" && args.isArray() && args.size() >= 2) {
    scrollTowards(entry,
                  args[0].asDouble(),
                  args[1].asDouble(),
                  args.size() >= 3 && args[2].asBool());
    return true;
  }

  if (name == "scrollToEnd") {
    scrollTowards(entry,
                  entry.contentSize.width - entry.containerSize.width,
                  entry.contentSize.height - entry.containerSize.height,
                  args.isArray() && args.size() >= 1 && args[0].asBool());
    return true;
  }

  if (name == "flashScrollIndicators") {
    // Nothing to flash: the indicator is always on screen while there is one to
    // draw -- see updateIndicators above. Claimed and ignored rather than left
    // to fall through, which would log an unimplemented command every time a
    // list settles.
    return true;
  }

  return false;
}

} // namespace basalt
