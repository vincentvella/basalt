#include "Win32ScrollView.h"

#include <unordered_map>
#include <utility>

#include "PlatformServices.h"

#include <react/renderer/components/scrollview/ScrollEvent.h>
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/core/ConcreteState.h>

#include <algorithm>
#include <chrono>
#include <string_view>

namespace basalt {

using facebook::react::EdgeInsets;
using facebook::react::Float;
using facebook::react::Point;
using facebook::react::ScrollEndDragEvent;
using facebook::react::ScrollEvent;
using facebook::react::ScrollViewEventEmitter;
using facebook::react::ScrollViewProps;
using facebook::react::ScrollViewShadowNode;
using facebook::react::ShadowView;
using facebook::react::Size;
using facebook::react::Tag;
using win32::RnWin32View;

namespace {

// How long a run of wheel notches may pause before it counts as over. Long
// enough that turning a wheel steadily is one drag rather than a dozen; short
// enough that letting go feels like letting go. Neither other platform needs a
// number here, because both are told.
constexpr double kWheelIdleMs = 150.0;

// One axis of the two inset props, in the order core/ScrollBounds.h wants them.
ScrollAxisInsets verticalInsets(const facebook::react::EdgeInsets &insets) {
  return {insets.top, insets.bottom};
}

ScrollAxisInsets horizontalInsets(const facebook::react::EdgeInsets &insets) {
  return {insets.left, insets.right};
}

double nowMilliseconds() {
  const auto since = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration<double, std::milli>(since).count();
}

} // namespace

Win32ScrollViewManager::Win32ScrollViewManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {}

Win32ScrollViewManager::~Win32ScrollViewManager() {
  *alive_ = false;
}

// ---------------------------------------------------------------------------
// Mutation handling
// ---------------------------------------------------------------------------

void Win32ScrollViewManager::update(RnWin32View *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;

  // A ScrollView is the one view that must clip: its content is deliberately
  // larger than its frame, and without this it paints over its siblings. Set
  // here rather than left to `overflow`, and after applyProps has run, so that
  // a ScrollView whose props say `visible` still behaves like one.
  view->setClipsChildren(true);

  entry.containerSize = shadowView.layoutMetrics.frame.size;

  if (const auto props = std::dynamic_pointer_cast<const ScrollViewProps>(shadowView.props)) {
    entry.scrollEnabled = props->scrollEnabled;
    entry.showsVerticalIndicator = props->showsVerticalScrollIndicator;
    entry.showsHorizontalIndicator = props->showsHorizontalScrollIndicator;
    entry.contentInset = props->contentInset;
    entry.indicatorInset = props->scrollIndicatorInsets;
    entry.eventThrottleMs = static_cast<double>(props->scrollEventThrottle);

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
  entry.offsetX = clampScrollOffset(entry.offsetX,
                                    entry.containerSize.width,
                                    entry.contentSize.width,
                                    horizontalInsets(entry.contentInset));
  entry.offsetY = clampScrollOffset(entry.offsetY,
                                    entry.containerSize.height,
                                    entry.contentSize.height,
                                    verticalInsets(entry.contentInset));
  view->setScrollOffset(static_cast<float>(entry.offsetX), static_cast<float>(entry.offsetY));
  // Here as well as in applyOffset: a list that grew or a window that was
  // resized changes the thumb without changing the offset at all.
  updateIndicators(entry);
}

void Win32ScrollViewManager::remove(Tag tag) {
  // The view is about to be deleted, so the entry must go with it: an idle
  // timer still in flight looks the tag up again and finds nothing, which is
  // the whole reason it is keyed by tag rather than holding the Entry.
  //
  // An animation timer is not idle, though, and the map it registered in
  // outlives the entry -- so it is killed here rather than left to expire
  // against a tag that no longer resolves.
  const auto it = entries_.find(tag);
  if (it != entries_.end()) {
    stopAnimation(it->second);
  }
  entries_.erase(tag);
}

// ---------------------------------------------------------------------------
// The wheel
// ---------------------------------------------------------------------------

bool Win32ScrollViewManager::scrollAt(RnWin32View *root, double x, double y, double dx, double dy) {
  if (root == nullptr || entries_.empty()) {
    return false;
  }
  RnWin32View *hit = win32::hitTest(root, static_cast<float>(x), static_cast<float>(y));

  // Innermost first, which is what makes a list inside a list scroll the inner
  // one. GTK and AppKit get the same ordering from their toolkits; here it is
  // the parent walk.
  for (RnWin32View *view = hit; view != nullptr; view = view->parent()) {
    const auto it = entries_.find(static_cast<Tag>(view->tag()));
    if (it == entries_.end()) {
      continue;
    }
    if (scrollEntry(it->second, dx, dy)) {
      return true;
    }
  }
  return false;
}

// Consumed means "this is a ScrollView and it is enabled", not "it moved" --
// matching AppKit. A list already at its bottom still eats the wheel rather
// than handing it to the list behind it, which is what every desktop does and
// what stops a nested list from dragging its parent along at the end of every
// scroll.
bool Win32ScrollViewManager::scrollEntry(Entry &entry, double dx, double dy) {
  if (!entry.scrollEnabled) {
    return false;
  }
  // The person moving the list wins over the app moving it.
  stopAnimation(entry);

  if (!entry.dragging) {
    entry.dragging = true;
    emitScrollEvent(entry, "beginDrag");
  }

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

  // Last, because with no UI thread installed -- which is every test --
  // postDelayed runs its work inline, and this ends the drag before returning.
  const Tag tag = entry.tag;
  const std::uint64_t generation = ++entry.wheelGeneration;
  postDelayed(kWheelIdleMs, [this, alive = alive_, tag, generation] {
    if (*alive) {
      endWheelDrag(tag, generation);
    }
  });
  return true;
}

void Win32ScrollViewManager::endWheelDrag(Tag tag, std::uint64_t generation) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  Entry &entry = it->second;
  if (entry.wheelGeneration != generation || !entry.dragging) {
    return;
  }
  entry.dragging = false;
  emitScrollEvent(entry, "endDrag");
  // A wheel supplies no velocity, so there is never a flick to take over from
  // here: the settle is always to the nearest point. That is also why this host
  // models no momentum -- see the header.
  settleOnSnapPoint(entry, 0.0);
}

// ---------------------------------------------------------------------------
// Offset, events and state
// ---------------------------------------------------------------------------

namespace {

// Which manager and view a timer is animating. A TIMERPROC carries no state, so
// the id is the key -- and the map is the reason `stopAnimation` must run before
// an entry goes, exactly as the display link does on AppKit.
std::unordered_map<UINT_PTR, std::pair<Win32ScrollViewManager *, facebook::react::Tag>> &
animations() {
  static std::unordered_map<UINT_PTR, std::pair<Win32ScrollViewManager *, facebook::react::Tag>>
      value;
  return value;
}

} // namespace

void CALLBACK Win32ScrollViewManager::onAnimationTimer(HWND /*hwnd*/,
                                                       UINT /*message*/,
                                                       UINT_PTR id,
                                                       DWORD now) {
  const auto it = animations().find(id);
  if (it == animations().end()) {
    KillTimer(nullptr, id);
    return;
  }
  Win32ScrollViewManager *manager = it->second.first;
  const facebook::react::Tag tag = it->second.second;
  manager->advanceAnimation(tag, static_cast<double>(now) / 1000.0);
}

void Win32ScrollViewManager::scrollTowards(Entry &entry, double x, double y, bool animated) {
  stopAnimation(entry);
  if (!animated || !entry.animation.start(entry.offsetX, entry.offsetY, x, y)) {
    applyOffset(entry, x, y, true);
    return;
  }
  // Sixteen milliseconds: this host's choreographer runs on a timer of the same
  // period, and matching it keeps a scroll and an animation stepping together
  // rather than beating against each other.
  entry.animationTimer = SetTimer(nullptr, 0, 16, onAnimationTimer);
  if (entry.animationTimer == 0) {
    entry.animation.stop();
    applyOffset(entry, x, y, true);
    return;
  }
  entry.animationLastMillis = 0;
  animations()[entry.animationTimer] = {this, entry.tag};
}

bool Win32ScrollViewManager::settleOnSnapPoint(Entry &entry, double velocityY) {
  const auto target = scrollSnapTarget(entry.snap,
                                       entry.offsetY,
                                       velocityY,
                                       entry.containerSize.height,
                                       entry.contentSize.height);
  if (!target) {
    return false;
  }
  scrollTowards(entry, entry.offsetX, *target, true);
  return true;
}

void Win32ScrollViewManager::stopAnimation(Entry &entry) {
  entry.animation.stop();
  if (entry.animationTimer != 0) {
    KillTimer(nullptr, entry.animationTimer);
    animations().erase(entry.animationTimer);
    entry.animationTimer = 0;
  }
}

void Win32ScrollViewManager::advanceAnimation(facebook::react::Tag tag, double nowSeconds) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  Entry &entry = it->second;
  if (!entry.animation.isRunning()) {
    stopAnimation(entry);
    return;
  }

  // The tick carries the system time rather than a delta, so the first frame
  // has nothing to measure against and advances by one period.
  const auto nowMillis = static_cast<unsigned long long>(nowSeconds * 1000.0);
  const double seconds = entry.animationLastMillis == 0
      ? 0.016
      : static_cast<double>(nowMillis - entry.animationLastMillis) / 1000.0;
  entry.animationLastMillis = nowMillis;

  double x = entry.offsetX;
  double y = entry.offsetY;
  const bool running = entry.animation.advance(seconds, x, y);
  applyOffset(entry, x, y, true);
  if (!running) {
    stopAnimation(entry);
  }
}

// The overlay scrollbars. The geometry is core/ScrollIndicator.h's, so GTK,
// AppKit and Win32 place the same thumb in the same place.
//
// Always drawn, rather than faded in while scrolling and out after: there is no
// timer here and no animation, which is why `flashScrollIndicators` stays a
// no-op -- there is nothing to flash something already on screen.
void Win32ScrollViewManager::updateIndicators(const Entry &entry) {
  if (entry.view == nullptr) {
    return;
  }

  const ScrollIndicator vertical =
      entry.showsVerticalIndicator
          ? scrollIndicatorFor(entry.containerSize.height,
                               entry.contentSize.height,
                               entry.offsetY,
                               verticalInsets(entry.contentInset),
                               verticalInsets(entry.indicatorInset))
          : ScrollIndicator{};
  const ScrollIndicator horizontal =
      entry.showsHorizontalIndicator
          ? scrollIndicatorFor(entry.containerSize.width,
                               entry.contentSize.width,
                               entry.offsetX,
                               horizontalInsets(entry.contentInset),
                               horizontalInsets(entry.indicatorInset))
          : ScrollIndicator{};

  entry.view->setScrollIndicators(static_cast<float>(vertical.offset),
                                  static_cast<float>(vertical.length),
                                  static_cast<float>(horizontal.offset),
                                  static_cast<float>(horizontal.length));
}

void Win32ScrollViewManager::applyOffset(Entry &entry, double x, double y, bool emitEvent) {
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

  if (entry.view != nullptr) {
    entry.view->setScrollOffset(static_cast<float>(clampedX), static_cast<float>(clampedY));
  }
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
  const double now = nowMilliseconds();
  if (entry.eventThrottleMs > 0 && now - entry.lastEmitMs < entry.eventThrottleMs) {
    return;
  }
  entry.lastEmitMs = now;
  emitScrollEvent(entry, "scroll");
}

void Win32ScrollViewManager::emitScrollEvent(Entry &entry, const char *which) {
  const auto emitter = std::dynamic_pointer_cast<const ScrollViewEventEmitter>(lookup_(entry.tag));
  if (emitter == nullptr) {
    return;
  }

  const auto fill = [&entry](ScrollEvent &event) {
    event.contentOffset = Point{.x = static_cast<Float>(entry.offsetX),
                                .y = static_cast<Float>(entry.offsetY)};
    event.contentSize = entry.contentSize;
    event.containerSize = entry.containerSize;
    event.contentInset = entry.contentInset;
    // Not a default worth trusting: ScrollEvent initialises zoomScale to 0, and
    // VirtualizedList only repairs negative values, so leaving it would make
    // every list measurement come out as zero.
    event.zoomScale = 1.0F;
    event.timestamp = static_cast<Float>(nowMilliseconds() / 1000.0);
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
  if (std::string_view(which) == "beginDrag") {
    emitter->onScrollBeginDrag(event);
  } else {
    emitter->onScroll(event);
  }
}

void Win32ScrollViewManager::writeStateOffset(const Entry &entry) {
  if (entry.state == nullptr) {
    return;
  }
  const Point offset{.x = static_cast<Float>(entry.offsetX),
                     .y = static_cast<Float>(entry.offsetY)};

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

bool Win32ScrollViewManager::dispatchCommand(Tag tag,
                                             const std::string &name,
                                             const folly::dynamic &args) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;

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
