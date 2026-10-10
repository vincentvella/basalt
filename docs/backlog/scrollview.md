# ScrollView

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (5):**

1. Trackpad (pixel-unit) scrolling is unverified; the wheel path is, on X11
2. contentBoundingRect
3. disableViewCulling is never set, which will matter once AT-SPI lands
4. No zoom
5. ~~`ScrollViewProps` has no rows on the support page~~
6. Three `<ScrollView>` props are a desktop question nobody has answered;
   ~~the three about snapping and contentOffset~~ are done

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

- ~~**`ScrollViewProps` has no rows on the support page.**~~ Done 2026-10-09:
  thirty-nine rows from `BaseScrollViewProps`, which closes the last group of
  this shape. Ten are read by every host, measured by grepping each one rather
  than trusting this file: `scrollEnabled`, `pagingEnabled`, both indicator
  flags, `scrollEventThrottle`, `contentInset`, `scrollIndicatorInsets` and the
  three snapping props. `decelerationRate` is read on GTK alone, which is the
  honest shape of the momentum work: AppKit takes the system's own deceleration
  and Windows has none yet.

  Of the twenty-eight nothing reads, twenty-one are somebody else's platform or
  a gesture no desktop has, and are marked as deliberately not done: the zoom
  family against entry 4, the bounce family because a desktop scroll view has no
  rubber band to stretch, iOS's inset-adjustment and `scrollsToTop`, Android's
  `persistentScrollbar`, and `keyboardDismissMode`, which wants a soft keyboard.
  `horizontal` is read by nothing on purpose and that is worth knowing: the axis
  is derived from the content's size against the viewport's, so a horizontal
  list works without the hint.

  The other seven are entry 6 below.

  What the entry said when it was open: a ReactCommon struct nothing scrapes, so
  the props an app writes on a `<ScrollView>` were neither
  implemented-and-ticked nor missing-and-recorded. They were absent.

  `BaseScrollViewProps.h` has the same `#pragma mark - Props` shape the scraped
  structs have, so the scrape is one entry in `STRUCTS` in
  `scripts/scrape_props.py`. The judgement per field is the work, and a good
  deal of it is already known from the entries in this file: the indicators,
  momentum and elasticity, snapping and `contentInset` all landed between
  2026-10-05 and 2026-10-08, with Windows behind on some of them.

  Worth doing the same way the other three were: grep each host for the prop
  rather than trusting this file, which is how the `<TextInput>` rows found two
  stale claims in backlog/textinput.md on the day they were added.

- **Three `<ScrollView>` props are a desktop question nobody has answered.**
  Seven of them were, until 2026-10-10; the three about snapping and
  `contentOffset` are done, and the entry keeps the others. Surfaced on 2026-10-09 by giving every prop a row,
  which is the point of the rows: each of these is a thing an app can write that
  no host reads, and none of them is somebody else's platform.

  ~~`contentOffset`~~ is done, 2026-10-10, and the entry had it half wrong,
  which is worth keeping. It said the initial value was dropped. It was not:
  `ScrollViewShadowNode::initialStateData` seeds the state from the prop, and
  all three hosts adopt the state's offset on first sight -- so a list did open
  where the app asked. What no host read was a *change* to the prop afterwards,
  which is the other thing an app writes it for.

  Applied the way upstream applies it, which is
  `oldScrollViewProps.contentOffset != newScrollViewProps.contentOffset` in
  `RCTScrollViewComponentView`: when the prop changes, and never merely when it
  differs from where the list is. The difference is the whole of it, and the
  reason is the one `<TextInput>`'s `text` has -- React Native re-renders for all
  sorts of reasons and each one carries the same `contentOffset` the app wrote
  once, so a host comparing against the list would drag it back under the person
  reading it. A test per host says so.

  Set rather than animated to, which is what assigning `UIScrollView.contentOffset`
  does, and it takes the list off a fling or an animated `scrollTo` first: an
  app that asks for an offset means that offset, not that offset plus wherever
  the coast was heading.

  One difference from iOS, written down rather than fixed: no `onScroll` is
  emitted for an offset the props asked for. Assigning `UIScrollView.contentOffset`
  there runs `scrollViewDidScroll`, which reports one. Here it goes through the
  same path the initial adoption does -- clamp, then set the offset on the view
  -- and that path reports nothing. An app that wrote the offset already knows
  where it is; what it would miss is a `contentOffset` clamped by the content
  being shorter than it asked for.

  `maintainVisibleContentPosition` is the one that matters most for a chat
  list, and the hardest: it keeps the visible content still while items are
  prepended, which means comparing content sizes across a mount and adjusting
  the offset before anything is drawn. The shape of it belongs in `core/`, since
  the arithmetic is the same on all three.

  ~~`snapToStart`, `snapToEnd` and `disableIntervalMomentum`~~ are done,
  2026-10-10, in `core/ScrollSnap.h` as this entry predicted -- with one thing
  it did not predict, which is the interesting part.

  The first two were the line or two expected: the content's own start and end
  count as snap points beside the listed ones, which is what upstream means by
  "by default the beginning of the list counts as a snap offset", and `false`
  frees the gap between an edge and the listed point nearest it. Freeing it
  means answering with *no* target at all, which each host already treats as
  "not a snapping gesture" and runs the fling for. Both halves of upstream's
  rule are kept, including the one that is easy to drop: a list thrown from
  inside the snapping region towards a freed edge still settles on the last
  listed point rather than sailing into the gap.

  **`disableIntervalMomentum` turned out to be about what this repository was
  already doing.** A flick here settled on the point next to where the finger
  left, however hard the throw -- which is `disableIntervalMomentum: true`
  behaviour, so the prop's *default* was the missing half. `snapToInterval` and
  `snapToOffsets` on iOS let the momentum carry across several points and
  settle at the one it arrives near, and that needed a projection: where the
  fling *would* have landed. `core/ScrollMomentum.h` grew
  `scrollMomentumDistance`, the closed form of the decay it already steps
  through, and a test asserts the two agree to within a few per cent -- an
  integral that drifted from the frames would settle a list somewhere it was
  never heading.

  `pagingEnabled` is deliberately left out of that: a paged view turns one page
  per flick on iOS whatever it was thrown at, and a test says so.

  Where upstream does this is `RCTEnhancedScrollView`'s
  `scrollViewWillEndDragging`, which is handed iOS's own predicted target rather
  than projecting one. Ten tests in `tests/test_scroll_snap.cpp` and three in
  `tests/test_momentum.cpp`, all arithmetic, which is most of the reason this
  lives in core.

  `centerContent` centres content smaller than the viewport, and
  `indicatorStyle` asks for a light or dark indicator, which these hosts draw
  themselves from `core/ScrollIndicator.h` and could colour from the prop.