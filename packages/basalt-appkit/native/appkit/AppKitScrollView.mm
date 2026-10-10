#import "AppKitScrollView.h"

#import <QuartzCore/QuartzCore.h>

#include <react/renderer/components/scrollview/ScrollEvent.h>
#include <react/renderer/components/scrollview/ScrollViewEventEmitter.h>
#include <react/renderer/components/scrollview/ScrollViewProps.h>
#include <react/renderer/core/ConcreteState.h>

#include <algorithm>
#include <cmath>

// A wheel notch carries no pixel distance of its own, so a step has to be
// chosen. The same 53 the GTK side uses -- roughly three lines of 16pt text --
// so one notch moves a list by the same amount on both desktops. Matching the
// other platform matters more here than matching either toolkit's own default,
// which is the whole argument of this project in one constant.
static constexpr double kWheelStepPixels = 53.0;

// The bridge between AppKit's protocol and the C++ manager, for the same reason
// the touch dispatcher has one: a C++ object cannot conform to an Objective-C
// protocol, and the views hold their handler weakly.
// The display link's target for an animated `scrollTo`. One per animating
// scroll view rather than one for all of them: the link belongs to the view it
// animates, which is what keeps it on that view's display and stops it when the
// view goes.
@interface RnAppKitScrollAnimationTarget : NSObject
@property(nonatomic, assign) basalt::AppKitScrollViewManager *manager;
@property(nonatomic, assign) facebook::react::Tag tag;
@property(nonatomic, assign) CFTimeInterval last;
- (void)step:(CADisplayLink *)sender;
@end

@implementation RnAppKitScrollAnimationTarget

- (void)step:(CADisplayLink *)sender {
  if (_manager == nullptr) {
    return;
  }
  // The frame's own timestamp, for the same reason GTK uses the frame clock's:
  // a callback that runs late must not shorten the curve.
  const CFTimeInterval now = sender.targetTimestamp;
  const double seconds = _last == 0 ? 0 : now - _last;
  _last = now;
  _manager->advanceAnimation(_tag, seconds);
}

@end

@interface RnAppKitScrollTarget : NSObject <RnAppKitScrollHandler>
@property(nonatomic, assign) basalt::AppKitScrollViewManager *manager;
@end

@implementation RnAppKitScrollTarget

- (BOOL)rnScrollView:(RnAppKitView *)view
                  by:(NSPoint)delta
             precise:(BOOL)precise
               phase:(NSEventPhase)phase
            momentum:(NSEventPhase)momentum {
  if (_manager == nullptr) {
    return NO;
  }
  // A touchpad reports phases; a wheel reports none. The two streams do not
  // overlap: while the fingers are down `momentum` is NSEventPhaseNone, and
  // once the system is coasting `phase` is. So a drag and the momentum after it
  // are told apart here and nowhere else.
  const bool began = phase == NSEventPhaseBegan;
  const bool ended = phase == NSEventPhaseEnded || phase == NSEventPhaseCancelled;
  const bool momentumBegan = momentum == NSEventPhaseBegan;
  const bool momentumEnded =
      momentum == NSEventPhaseEnded || momentum == NSEventPhaseCancelled;

  // A precise device reports pixels; a wheel reports line counts. Treating a
  // line as a pixel makes the wheel move the content by one pixel a notch,
  // which reads as the wheel not working.
  const double scale = precise ? 1.0 : kWheelStepPixels;

  // AppKit's positive Y is a scroll *up*, which moves content down and reduces
  // the offset. React Native's contentOffset grows downward, like GTK's delta,
  // so both axes are inverted here. scrollingDeltaY already accounts for the
  // user's natural-scrolling preference, so nothing else has to.
  return _manager->scrollBy(static_cast<facebook::react::Tag>(view.rnTag),
                            -delta.x * scale,
                            -delta.y * scale,
                            began,
                            ended,
                            momentumBegan,
                            momentumEnded);
}

@end

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

namespace {

// One axis of the two inset props, in the order core/ScrollBounds.h wants them.
ScrollAxisInsets verticalInsets(const facebook::react::EdgeInsets &insets) {
  return {insets.top, insets.bottom};
}

ScrollAxisInsets horizontalInsets(const facebook::react::EdgeInsets &insets) {
  return {insets.left, insets.right};
}

double nowSeconds() {
  return NSProcessInfo.processInfo.systemUptime;
}

} // namespace

AppKitScrollViewManager::AppKitScrollViewManager(EmitterLookup lookup) : lookup_(std::move(lookup)) {
  RnAppKitScrollTarget *target = [[RnAppKitScrollTarget alloc] init];
  target.manager = this;
  scrollTarget_ = target;
}

AppKitScrollViewManager::~AppKitScrollViewManager() {
  ((RnAppKitScrollTarget *)scrollTarget_).manager = nullptr;
  scrollTarget_ = nil;
}

// ---------------------------------------------------------------------------
// Mutation handling
// ---------------------------------------------------------------------------

void AppKitScrollViewManager::update(RnAppKitView *view, const ShadowView &shadowView) {
  const Tag tag = shadowView.tag;
  auto [it, inserted] = entries_.try_emplace(tag);
  Entry &entry = it->second;

  entry.view = view;
  entry.tag = tag;

  // A ScrollView is the one view that must clip: its content is deliberately
  // larger than its frame, and without this it paints over its siblings.
  [view setRnClipsChildren:YES];
  view.rnScrollHandler = (id<RnAppKitScrollHandler>)scrollTarget_;

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
    // The three modifiers of snapping, all of which `core/ScrollSnap.h` acts
    // on: whether the content's own edges count as snap points beside the
    // listed ones, and whether a fling across a spacing carries or stops at the
    // next point.
    // `centerContent`, applied below once the content size is known.
    entry.centerContent = props->centerContent;

    // `maintainVisibleContentPosition`, remembered for the next transaction:
    // the work it asks for happens around the mutations rather than while a
    // prop is being read. See `prepareMaintainVisiblePosition`.
    entry.maintainVisible = props->maintainVisibleContentPosition;

    // `indicatorStyle`, resolved to a colour by core so that all three hosts
    // draw the same thumb for the same prop.
    switch (props->indicatorStyle) {
      case facebook::react::ScrollViewIndicatorStyle::White:
        entry.indicatorStyle = ScrollIndicatorStyle::White;
        break;
      case facebook::react::ScrollViewIndicatorStyle::Black:
        entry.indicatorStyle = ScrollIndicatorStyle::Black;
        break;
      case facebook::react::ScrollViewIndicatorStyle::Default:
      default:
        entry.indicatorStyle = ScrollIndicatorStyle::Default;
        break;
    }

    entry.snap.snapToStart = props->snapToStart;
    entry.snap.snapToEnd = props->snapToEnd;
    entry.snap.disableIntervalMomentum = props->disableIntervalMomentum;

    // `contentOffset`, which an app writes to open a list part way down. Its
    // first value reached here through the state and is adopted below; this is
    // every later change, applied the way upstream applies it --
    // `oldScrollViewProps.contentOffset != newScrollViewProps.contentOffset` in
    // `RCTScrollViewComponentView` -- rather than whenever the prop differs
    // from where the list is.
    //
    // The offset is set rather than animated to, which is what assigning
    // `UIScrollView.contentOffset` does, and the clamp below is what keeps a
    // value past the end of the content from showing empty space.
    if (!entry.lastPropContentOffset.has_value()
        || *entry.lastPropContentOffset != props->contentOffset) {
      entry.lastPropContentOffset = props->contentOffset;
      entry.offsetX = props->contentOffset.x;
      entry.offsetY = props->contentOffset.y;
      // Whatever the list was animating towards is somewhere the app has just
      // said it does not want to be. No fling to stop: the momentum on this
      // host is not this host's to run.
      stopAnimation(entry);
    }
    // `decelerationRate` is not read on this host, so the projection of where a
    // fling would land uses React Native's own default -- which is what the
    // config carries. See the support page's row for the prop.
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

  // `centerContent`: content smaller than its container sits in the middle of
  // it. Expressed as an inset of half the slack at each end, which leaves the
  // range below a single offset to be at -- so the clamp that was already here
  // is what centres the content and what keeps it centred.
  //
  // It replaces the app's own `contentInset` rather than adding to it, which is
  // what iOS does: `centerContentIfNeeded` assigns the inset outright, and
  // `RCTScrollViewComponentView` skips the `contentInset` prop entirely while
  // `centerContent` is set. Recomputed here rather than where the prop is read
  // because it needs the content size, which arrives with the state.
  if (entry.centerContent) {
    const ScrollAxisInsets horizontal =
        centeringInsets(entry.containerSize.width, entry.contentSize.width);
    const ScrollAxisInsets vertical =
        centeringInsets(entry.containerSize.height, entry.contentSize.height);
    entry.contentInset.left = static_cast<facebook::react::Float>(horizontal.leading);
    entry.contentInset.right = static_cast<facebook::react::Float>(horizontal.trailing);
    entry.contentInset.top = static_cast<facebook::react::Float>(vertical.leading);
    entry.contentInset.bottom = static_cast<facebook::react::Float>(vertical.trailing);
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
  [view setRnScrollOffsetX:x y:y];
  // Here as well as in applyOffset: a list that grew or a window that was
  // resized changes the thumb without changing the offset at all.
  updateIndicators(entry);
}

// ---------------------------------------------------------------------------
// maintainVisibleContentPosition
//
// Two halves around a mounting transaction: measure the child being held still
// before the mutations, and move the offset by however far it moved after. The
// arithmetic -- which child, how far, and whether the list was near enough to
// the start to follow the new content instead -- is core/ScrollVisiblePosition.h.
//
// What is here is the part only a toolkit knows: where this host's children
// are. React Native wraps a ScrollView's children in exactly one content view,
// so the rows are that view's children.
// ---------------------------------------------------------------------------

namespace {

// Whether the list scrolls sideways, decided the way upstream decides it: by
// the content being wider than the view rather than by a prop, there being no
// prop for it.
bool scrollsHorizontally(const Size &container, const Size &content) {
  return content.width > container.width;
}

} // namespace

std::vector<ScrollChildBox> AppKitScrollViewManager::contentChildBoxes(const Entry &entry) const {
  std::vector<ScrollChildBox> boxes;
  if (entry.view == nil) {
    return boxes;
  }
  // The content view, which React Native always makes and always makes exactly
  // one of. `rnChildrenInPaintOrder` rather than `subviews` because the overlay
  // scrollbar is a subview too and is not a row; for children with no zIndex --
  // which rows do not have -- that order is the order they were inserted in.
  RnAppKitView *const content = [entry.view rnChildrenInPaintOrder].firstObject;
  if (content == nil) {
    return boxes;
  }

  const bool horizontal = scrollsHorizontally(entry.containerSize, entry.contentSize);
  for (RnAppKitView *row in [content rnChildrenInPaintOrder]) {
    const NSRect frame = row.frame;
    boxes.push_back(ScrollChildBox{
        .tag = static_cast<int>(row.rnTag),
        .leading = horizontal ? frame.origin.x : frame.origin.y,
        .length = horizontal ? frame.size.width : frame.size.height});
  }
  return boxes;
}

void AppKitScrollViewManager::prepareMaintainVisiblePosition() {
  for (auto &[tag, entry] : entries_) {
    entry.pinned = {};
    if (!entry.maintainVisible.has_value()) {
      continue;
    }
    const bool horizontal = scrollsHorizontally(entry.containerSize, entry.contentSize);
    entry.pinned = firstVisibleChild(contentChildBoxes(entry),
                                     entry.maintainVisible->minIndexForVisible,
                                     horizontal ? entry.offsetX : entry.offsetY);
  }
}

void AppKitScrollViewManager::adjustForMaintainVisiblePosition() {
  for (auto &[tag, entry] : entries_) {
    if (!entry.maintainVisible.has_value() || !entry.pinned.has) {
      continue;
    }
    const bool horizontal = scrollsHorizontally(entry.containerSize, entry.contentSize);
    const double offset = horizontal ? entry.offsetX : entry.offsetY;

    // The same child, found again by tag: its *position in the list* is exactly
    // what the mutation may have changed, so an index would find a different
    // row. A child that is gone is not adjusted for, which is upstream's
    // behaviour too -- it gives up rather than guessing.
    const ScrollPinnedChild pinned = entry.pinned;
    entry.pinned = {};
    std::optional<double> leading;
    for (const ScrollChildBox &box : contentChildBoxes(entry)) {
      if (box.tag == pinned.tag) {
        leading = box.leading;
        break;
      }
    }
    if (!leading.has_value()) {
      continue;
    }

    const ScrollVisibleAdjustment adjustment = visiblePositionAdjustment(
        pinned.leading, *leading, offset, entry.maintainVisible->autoscrollToTopThreshold);
    if (!adjustment.move) {
      continue;
    }

    if (horizontal) {
      applyOffset(entry, offset + adjustment.delta, entry.offsetY, true);
    } else {
      applyOffset(entry, entry.offsetX, offset + adjustment.delta, true);
    }

    // And the start, animated, when the list was near enough to it to be
    // following the new content rather than holding its place.
    if (adjustment.autoscrollToStart) {
      if (horizontal) {
        scrollTowards(entry, 0.0, entry.offsetY, true);
      } else {
        scrollTowards(entry, entry.offsetX, 0.0, true);
      }
    }
  }
}

void AppKitScrollViewManager::remove(Tag tag) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  // The view's reference to the target is weak and the target dispatches by
  // tag, so dropping the entry is enough for the wheel: one arriving afterwards
  // finds no entry and is passed up the responder chain instead.
  //
  // A display link is not like that. It is retained by the run loop and holds a
  // tag it would go on dispatching, so it has to be invalidated here rather than
  // dropped -- the same lesson as the touch dispatcher's controllers.
  stopAnimation(it->second);
  entries_.erase(it);
}

// ---------------------------------------------------------------------------
// The wheel
// ---------------------------------------------------------------------------

bool AppKitScrollViewManager::scrollBy(Tag tag,
                                       double dx,
                                       double dy,
                                       bool began,
                                       bool ended,
                                       bool momentumBegan,
                                       bool momentumEnded) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return false;
  }
  Entry &entry = it->second;
  if (!entry.scrollEnabled) {
    return false;
  }
  // The person moving the list wins over the app moving it, which is what a
  // wheel or a two-finger drag during an animated `scrollTo` means.
  stopAnimation(entry);

  if (began && !entry.dragging) {
    entry.dragging = true;
    emitScrollEvent(entry, "beginDrag");
  }
  if (momentumBegan && !entry.coasting) {
    entry.coasting = true;
    emitScrollEvent(entry, "momentumBegin");
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

  // Already in pixels: the trampoline resolved lines against kWheelStepPixels,
  // because only the event knows which it was.
  applyOffset(entry, entry.offsetX + dx, entry.offsetY + dy, true);

  if (ended && entry.dragging) {
    entry.dragging = false;
    emitScrollEvent(entry, "endDrag");
    // A release. On this host the system may still be coasting, so the settle
    // below waits for it to finish -- but a release with no momentum at all
    // ends here and has to snap now.
    if (!entry.coasting) {
      settleOnSnapPoint(entry, 0.0);
    }
  }
  if (momentumEnded && entry.coasting) {
    entry.coasting = false;
    emitScrollEvent(entry, "momentumEnd");
    // macOS decelerates for us, so unlike GTK there is no velocity to take over
    // from: the system has already carried the list as far as it was thrown,
    // and what is left is to settle where it stopped.
    settleOnSnapPoint(entry, 0.0);
  }
  return true;
}

// ---------------------------------------------------------------------------
// Offset, events and state
// ---------------------------------------------------------------------------

void AppKitScrollViewManager::scrollTowards(Entry &entry, double x, double y, bool animated) {
  stopAnimation(entry);
  if (!animated || !entry.animation.start(entry.offsetX, entry.offsetY, x, y)) {
    // Not animated, or already there. Either way the offset is the answer.
    applyOffset(entry, x, y, true);
    return;
  }

  if (@available(macOS 14.0, *)) {
    RnAppKitScrollAnimationTarget *target = [[RnAppKitScrollAnimationTarget alloc] init];
    target.manager = this;
    target.tag = entry.tag;
    target.last = 0;
    CADisplayLink *link = [entry.view displayLinkWithTarget:target
                                                   selector:@selector(step:)];
    [link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    entry.displayLink = link;
    entry.animationLastSeconds = 0;
  } else {
    // Same trade the animation choreographer makes below 14: no frame source
    // worth carrying, so the scroll arrives rather than moves.
    entry.animation.stop();
    applyOffset(entry, x, y, true);
  }
}

bool AppKitScrollViewManager::settleOnSnapPoint(Entry &entry, double velocityY) {
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

void AppKitScrollViewManager::stopAnimation(Entry &entry) {
  entry.animation.stop();
  if (entry.displayLink != nil) {
    [(CADisplayLink *)entry.displayLink invalidate];
    entry.displayLink = nil;
  }
}

void AppKitScrollViewManager::advanceAnimation(facebook::react::Tag tag, double seconds) {
  const auto it = entries_.find(tag);
  if (it == entries_.end()) {
    return;
  }
  Entry &entry = it->second;
  if (!entry.animation.isRunning()) {
    stopAnimation(entry);
    return;
  }

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
void AppKitScrollViewManager::updateIndicators(const Entry &entry) {
  if (entry.view == nil) {
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

  const ScrollIndicatorColour thumb = scrollIndicatorColourFor(entry.indicatorStyle);
  [entry.view setRnScrollIndicatorColourRed:thumb.red
                                      green:thumb.green
                                       blue:thumb.blue
                                      alpha:thumb.alpha];
  [entry.view setRnScrollIndicatorVerticalOffset:vertical.offset
                                  verticalLength:vertical.length
                                horizontalOffset:horizontal.offset
                                horizontalLength:horizontal.length];
}

void AppKitScrollViewManager::applyOffset(Entry &entry, double x, double y, bool emitEvent) {
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

  [entry.view setRnScrollOffsetX:clampedX y:clampedY];
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
  const double now = nowSeconds();
  if (entry.eventThrottleMs > 0) {
    if (now - entry.lastEmitSeconds < entry.eventThrottleMs / 1000.0) {
      return;
    }
  }
  entry.lastEmitSeconds = now;
  emitScrollEvent(entry, "scroll");
}

void AppKitScrollViewManager::emitScrollEvent(Entry &entry, const char *which) {
  const auto emitter = std::dynamic_pointer_cast<const ScrollViewEventEmitter>(lookup_(entry.tag));
  if (emitter == nullptr) {
    return;
  }

  const auto fill = [&entry](ScrollEvent &event) {
    event.contentOffset =
        Point{.x = static_cast<Float>(entry.offsetX), .y = static_cast<Float>(entry.offsetY)};
    event.contentSize = entry.contentSize;
    event.containerSize = entry.containerSize;
    event.contentInset = entry.contentInset;
    // Not a default worth trusting: ScrollEvent initialises zoomScale to 0, and
    // VirtualizedList only repairs negative values, so leaving it would make
    // every list measurement come out as zero.
    event.zoomScale = 1.0F;
    event.timestamp = static_cast<Float>(nowSeconds());
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

void AppKitScrollViewManager::writeStateOffset(const Entry &entry) {
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

bool AppKitScrollViewManager::dispatchCommand(Tag tag,
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
