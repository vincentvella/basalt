# ScrollView

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (5):**

1. Trackpad (pixel-unit) scrolling is unverified; the wheel path is, on X11
2. contentBoundingRect
3. disableViewCulling is never set, which will matter once AT-SPI lands
4. No zoom
5. `ScrollViewProps` has no rows on the support page

- Trackpad (pixel-unit) scrolling is unverified; the wheel path is, on X11.
- ~~No momentum.~~ See the Input section. What is left is Windows, which has no
  fling velocity to model one from, and `onScrollEndDrag`'s velocity, which is
  still reported as zero on every host, so `ScrollView._isAnimating()` is
  still wrong.
- ~~**`animated: true` scrolls instantly.**~~ Done on all three:
  `core/ScrollAnimation.h` is a cubic ease-in-out over a fixed duration, which a
  fling deliberately is not: a fling has velocity and no target, this has a
  target and no velocity, and exponential friction approaches a destination
  without ever arriving at it.

  Each host steps it with what it already has: GTK a frame-clock tick callback
  beside the fling's, AppKit a `CADisplayLink` per animating view because the
  system does its own deceleration and there was no stepper to borrow, Windows a
  thread timer because the scroll manager has a tag rather than an HWND. A
  gesture or a wheel cancels it on all three, the person moving the list wins
  over the app moving it.
- ~~**No snapping or paging.**~~ Done on all three: `pagingEnabled`,
  `snapToInterval`, `snapToOffsets` and `snapToAlignment`, decided once in
  `core/ScrollSnap.h` and settled with the animation next door.

  The rule is "the next point in the direction it was flicked", which is
  symmetric and is what CSS scroll-snap does, twenty pixels into a page,
  flicked back, the answer is that page's start rather than the one before it.
  Going back two boundaries would let a small flick travel further than a large
  one.

  A snapping list does not coast: the settle replaces the fling, so the velocity
  decides *which* point rather than how far. On Windows there is never a flick
  to replace, because a wheel supplies no velocity.

  What is left here is `maintainVisibleContentPosition`, which is a different
  thing, keeping the offset stable while content is inserted above it.
- ~~**No scrollbars are drawn.**~~ Done on all three: an overlay indicator,
  `core/ScrollIndicator.h`, drawn rather than borrowed. Each toolkit has a
  scrollbar and none of them is available here: GTK's comes with
  `GtkScrolledWindow` and AppKit's `NSScroller` with `NSScrollView`, and this
  platform's scroll view is neither, because adopting one of those containers
  would mean giving it the scrolling too.

  So the geometry is decided in core, which is also what makes it testable: a
  scrollbar is pure paint, and paint is what these hosts cannot assert. Each
  host reports the thumb on the scroll view's own dump line as
  `scrollbar-v=(offset,length)`, and `showsVerticalScrollIndicator` and
  `showsHorizontalScrollIndicator` turn it off.

  On macOS it is a subview kept above the content rather than something
  `drawRect:` paints, because AppKit draws subviews over their superview and a
  ScrollView always has one covering it.

  What is left is that nothing here is a drag target: the thumb reports the
  position and cannot be used to change it. `flashScrollIndicators` is
  correspondingly still a no-op; the bar is always on screen, so there is
  nothing to flash.
- ~~**`contentInset` and `scrollIndicatorInsets` are reported and never
  applied.**~~ Done on all three, in `core/ScrollBounds.h`.

  An inset is not padding: it makes the *range* bigger and leaves the content
  the size it is, so the range now runs from `-leading` to
  `content - container + trailing`. With no insets those are the same numbers
  as before, which is why nothing changed for anybody who never set one.

  The two props do different jobs and are kept apart for that reason:
  `contentInset` changes how far the view scrolls, and therefore what fraction
  of the way through any offset is; `scrollIndicatorInsets` shortens the track
  the thumb runs in and nothing else. A host that applied one to both would
  pass a test that used the same number twice, so the scenario uses 60 and 30.

  It also removed three copies of `clampOffset`, one per host, none of which
  read the insets.
- **No zoom.** `zoomScale` is reported as 1 and nothing changes it.
  Pinch-to-zoom has no implementation on any of the three, and a desktop has no
  obvious gesture for it.
- `contentBoundingRect.origin` is assumed to be zero; iOS positions its
  container view at that origin.
- `disableViewCulling` is never set, which will matter once AT-SPI lands.

- **`ScrollViewProps` has no rows on the support page.** The last group of the
  shape the layout props, `<Image>` and `<TextInput>` each had: a ReactCommon
  struct nothing scrapes, so the props an app writes on a `<ScrollView>` are
  neither implemented-and-ticked nor missing-and-recorded. They are absent.

  `BaseScrollViewProps.h` has the same `#pragma mark - Props` shape the scraped
  structs have, so the scrape is one entry in `STRUCTS` in
  `scripts/scrape_props.py`. The judgement per field is the work, and a good
  deal of it is already known from the entries in this file: the indicators,
  momentum and elasticity, snapping and `contentInset` all landed between
  2026-10-05 and 2026-10-08, with Windows behind on some of them.

  Worth doing the same way the other three were: grep each host for the prop
  rather than trusting this file, which is how the `<TextInput>` rows found two
  stale claims in backlog/textinput.md on the day they were added.