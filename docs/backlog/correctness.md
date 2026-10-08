# Correctness gaps in what exists

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (3):**

1. ~~borderStyles, dashed and dotted borders~~
2. pointScaleFactor, fractional scaling under Wayland
3. 3D transforms have no perspective: gsk_transform_perspective exists, and Trans
4. Six view style props that no host reads, and nothing said so (box shadows,
   linear gradients and hitSlop are done on GTK and AppKit)

- ~~**`borderStyles`, dashed and dotted borders.**~~ Done on GTK 2026-10-07 and
  on AppKit 2026-10-08, with one limitation that is structural rather than
  unfinished.

  GTK's border node paints solid only, as the entry said, so dotted and dashed
  are a stroked path around the view's own rounded rectangle instead, and the
  stroke replaces the border rather than joining it: two would paint the outline
  twice and leave dashes sitting on a solid line. Inset by half the width,
  because a stroke straddles its path while a border node sits inside the box,
  and without that a 4pt dashed border would paint two points outside the view.

  The dash pattern is scaled to the border width the way a browser does it, since
  a fixed pattern reads as a hairline on a thick border and as a solid line on a
  thin one. Dotted is round caps on a zero-length dash, which is what makes a dot
  round rather than a short dash.

  **One style for the whole outline, not one per side.** A stroked path carries
  one dash pattern, so React Native's per-side `borderStyles` would need four
  paths and four strokes with the corners divided between them. The first side
  that asks for something other than solid decides the outline. A border with
  different styles per side is rare enough to be worth that, and saying so is
  better than four paths nobody asked for.

  Four tests, which walk the render tree and count border against stroke nodes,
  so they assert what was drawn rather than what was stored. Two of them are
  controls: a solid border is still a border node, and a style on a view with no
  border width draws nothing, which would otherwise put a line around every view
  that mentioned `borderStyle` and set no width.

  AppKit the next day, and the entry was wrong about what stood in the way. It
  said the borders are layer properties, so a dashed one would need a
  `CAShapeLayer`. They are not: they are drawn in `drawRect:` with CSS's own
  wedge algorithm, four filled edges clipped to the sectors they own. So the
  dashed case is one stroked path with the same dash pattern GTK uses, inset by
  half the width for the same reason, replacing the four fills rather than
  joining them. The corner radii are left unshrunk by the inset, matching what
  GSK's own inset does, so the two hosts stroke one shape.

  Six tests, against a bitmap rather than against what the view stored, counting
  runs of ink along the top edge: solid is one run corner to corner, dashed is
  several, and dotted is more of them than dashed at the same width because the
  pattern scales with the width in both cases. Sabotaging the branch fails three
  of them; giving dotted the dashed pattern fails one.

  **And the prop had never arrived, on either host.** React Native's
  `borderStyles` is a cascade of optionals with a slot per spelling, and
  `borderStyle: 'dashed'` written once for the whole border lands in `all`. Both
  hosts read `top`, `right`, `bottom` and `left` directly, found nothing, and drew
  solid: GTK's half had shipped the day before in exactly that state, with four
  render-node tests passing because each set the style on the widget by hand.
  `resolveBorderMetrics` already does the cascade, as it does for the widths and
  the colours, so both now read its answer. Both hosts print `border-style=` in
  the tree dump, there is a unit test per host that the dump says it, and an
  end-to-end scenario reads it out of e2e/views.js, which is also the app
  `compare_hosts.sh` diffs between desktops.
- `pointScaleFactor`, fractional scaling under Wayland.
- ~~`transformOrigin` is passed through but never exercised; the default centre
  anchor is.~~ Exercised now, in all three mounting suites with the same two
  tests and the same tags. It worked already, every host calls
  `resolveTransform`, which folds the origin in, so this closes a hole in
  the tests rather than in the feature.

  What makes it checkable with no display is that an anchor is a claim about
  a point: scaling about the top-left has to leave the top-left corner where
  it was. The matrix is in centre-relative coordinates, so that corner is at
  (-w/2, -h/2), and the test maps it through the matrix the dump printed.
  Replacing `resolveTransform` with a bare `props->transform` fails it.
- 3D transforms have no perspective, **and the first half of that sentence was
  wrong**. Measured 2026-10-07 rather than reasoned about, and the picture is
  narrower than it looked.

  What is already true: React Native folds `transform: [{perspective: N}, ...]`
  into the same 4x4 it sends for everything else, putting -1/N in the slot CSS
  matrix3d calls m34. `GtkMountingManager` copies all sixteen floats, and
  `gsk_transform_matrix` keeps them: a round trip through `GskTransform` and back
  returns a matrix that `graphene_matrix_is_2d` calls three-dimensional. So
  nothing needs to call `gsk_transform_perspective`, and "nothing sets it up" is
  not the problem.

  What is unknown: whether GSK *renders* with it. Two attempts to show that
  failed and are worth recording so the third does not repeat them.
  `gtk_widget_compute_point`, which the transform tests use, does not observe the
  term at all: flattening m34 at the widget gives byte-identical mapped points.
  And a paragraph of non-affinity that looks like perspective is not, because
  GTK's own projection of a 3D rotation to two dimensions is already non-affine,
  so a midpoint test passes on the rotation alone.

  Comparing a rotation against rotation-times-perspective is also no good:
  multiplying by a matrix with m34 set changes more terms than m34, so the two
  differ for reasons that are not perspective.

  What would settle it: a rendered comparison, either a pixel diff or a walk of
  the render node a transformed view produces, which is now possible, the clip
  tests having shown how to reach one. Hit testing is the wrong instrument and
  cannot answer it.

- **Six view style props that no host reads, and nothing said so.** Counted
  2026-10-08 by going through `BaseViewProps` field by field and grepping all
  three hosts for each, after `cursor` and `borderStyle` both turned out to be
  props the backlog thought were handled. These are the ones no host mentions
  anywhere, and which nothing in this backlog mentioned either:

  - ~~`boxShadow`~~, **done on GTK and AppKit 2026-10-08**; the older
    `shadowColor`, `shadowOffset`, `shadowOpacity` and `shadowRadius` beside it
    are still ignored, and are iOS's pre-CSS spelling of the same idea. What the
    work turned out to be is at the end of this entry.
  - `backgroundImage`: **linear gradients are done on GTK and AppKit
    2026-10-08**, radial ones are not, and `backgroundSize`,
    `backgroundPosition` and `backgroundRepeat` are still ignored. What the work
    turned out to be is at the end of this entry.
  - `filter`: the CSS filter functions on a view rather than on an image. Partly
    answerable with what the blur work already built: GSK has a blur node and a
    colour matrix, and Core Image has the rest.
  - `outlineColor`, `outlineWidth`, `outlineOffset` and `outlineStyle`. CSS's
    outline, which unlike a border takes no layout space and is drawn outside the
    box. The focus ring each host draws is the same idea and is hard-coded.
  - `mixBlendMode` and `isolation`.
  - ~~`hitSlop`~~, **done on GTK and AppKit 2026-10-08**. The one of the nine
    that is behaviour rather than decoration, so an app relying on it was wrong
    rather than plain. What the work turned out to be is at the end of this
    entry.
  - `shouldRasterize` and `removeClippedSubviews`: performance hints, and the
    only two of the nine where ignoring them is arguably correct.

  Not one list of work. Each is its own entry waiting to be written, and the
  count above treats them as one until somebody picks one up. What this entry is
  for is that none of them was written down at all, which is how `cursor` stayed
  missing while the backlog described it as covered.

  **The lesson that generalises** is in the two that were fixed: a prop with no
  line in the tree dump cannot be seen to arrive, and a unit test that sets the
  value on the widget by hand passes whether or not the prop path works. Every
  one of the nine needs its dump line and an end-to-end assertion, not only a
  drawing.

  **`boxShadow`, done on GTK and AppKit 2026-10-08.** The list crosses the seam
  as React Native wrote it, six fields per shadow, and each view layer decides
  what a shadow is made of there.

  GTK is a direct mapping and barely an implementation: GSK has an outset and an
  inset shadow node whose arguments are CSS's dx, dy, spread and blur, and they
  take the view's own rounded outline, so a rounded card casts a rounded shadow
  with nothing computing a rectangle. Outset shadows are appended before the
  background and inset ones after it, which is where CSS puts them, and the list
  is painted back to front because CSS says the first shadow is the one on top.
  One thing the mapping does not survive: GSK asserts a non-negative blur radius
  and `blurRadius: -8` parses and arrives, so the blur is clamped and the spread,
  which CSS does allow to be negative, is not. Found by a sabotage run that
  swapped the two and took the suite down with a Gsk-CRITICAL.

  AppKit is one CALayer per shadow with a `shadowPath` and a mask, which is how
  React Native's iOS half does it and the only way to paint outside a view at
  all: `drawRect:` is clipped to the bounds. The offset and spread are baked into
  the path rather than set as `shadowOffset`, which keeps a positive dy pointing
  down in a flipped view, and `shadowRadius` is half the CSS blur, the same
  sigma conversion the image blur measured. Corner radii grow by the spread
  through the curve the CSS spec gives, so a tight corner on a widely spread
  shadow does not become a circle.

  **Two things on the AppKit side are decisions rather than details.** An outset
  shadow normally sits inside the view, where it composites exactly as the view
  does; a view that clips its own layer -- `overflow: 'hidden'`, or radii too
  elliptical for `cornerRadius` -- would lose it entirely, since CSS clips
  neither, so for those it is cast into the parent's layer below the view
  instead. The cost is z-order: AppKit attaches a subview's layer lazily at the
  first display and these are built while mounting, so the shadow lands at the
  bottom of the parent rather than directly below its own view, which shows only
  where a sibling overlaps the shadow of a clipping view. And an inset shadow,
  being a sublayer, paints above this view's own drawn text where CSS puts it
  below; GTK gets that right for free.

  **No pixel test on macOS, and that is measured.** A layer's shadow is drawn by
  Core Animation while compositing; `-[CALayer renderInContext:]` draws none of
  it, confirmed on a bare layer with a `shadowPath` and nothing else, every pixel
  white inside the path and out. That is also what `AppKitSnapshot.mm` uses, so a
  box shadow is invisible to this project's own snapshots on macOS, and a
  CGWindowList capture needs a permission no runner grants. Seventeen unit tests
  across the two hosts assert the render nodes on one side and the layers, paths
  and masks on the other, plus an end-to-end scenario that reads the dump on
  both. Sabotages checked: a spread applied as a blur, dx and dy swapped, inset
  painted as outset, and a clipping view keeping its shadow inside.

  **`backgroundImage` as a linear gradient, done on GTK and AppKit 2026-10-08.**
  Two halves, and the interesting one is not the drawing.

  The *resolution* is specified and is shared: `core/Gradients.h` turns a
  `LinearGradient` and a box size into two points and a list of stops, and both
  hosts draw what it says. It is a port of React Native's own iOS code, which is
  a port of Chromium, because both halves are easy to get plausibly wrong. The
  gradient line is a perpendicular-bisector construction, not the box's diagonal:
  at 135 degrees on an 80x40 box it runs (10,-10) to (70,50), longer than the box
  and ending outside it, where the diagonal would be (0,0) to (80,40). A corner
  keyword is not 45 degrees either, except on a square. And the colour stops go
  through the fixup in css-images-4: a first stop with no position sits at 0 and a
  last one at 1, a position that goes backwards is pulled forward so that
  `red 60%, blue 20%` is a hard edge rather than a reversal, a run with no
  positions is spread evenly, and a transition hint -- `linear-gradient(red, 20%,
  blue)` -- becomes nine stops along the curve the spec gives, which is what moves
  the midpoint colour to the hint.

  The drawing is small on both. GSK has a linear-gradient node that takes the two
  points and the stops; Core Graphics has `CGGradient` and
  `CGContextDrawLinearGradient`, drawn in `drawRect:` with both extend flags set,
  which is what keeps the corners painted when the line is shorter than the box.
  Both clip to the view's rounded box, as the background colour does, and both
  paint above the background colour and below the content, where CSS puts a
  background image, back to front so the first in the list is on top.

  Eleven tests in core for the arithmetic, which is shared and so belongs there;
  six per host for the drawing, the GTK ones walking the render tree and the
  AppKit ones reading pixels out of a bitmap, since a gradient goes through
  `drawRect:` rather than through Core Animation; and an end-to-end scenario that
  asserts the resolved line on both. Sabotages checked: the two points swapped,
  the list drawn front to back, and the rounded clip taken away.

  **What is left of the prop.** A radial gradient parses in React Native and is
  dropped here, which needs the size keywords -- `closest-side`,
  `farthest-corner` -- and the position syntax resolved first; GSK and Core
  Graphics both have the node for it. `backgroundSize`, `backgroundPosition` and
  `backgroundRepeat` have nothing to act on until an image can be a background,
  which is a loader question rather than a drawing one.

  **`hitSlop`, done on GTK and AppKit 2026-10-08.** Four insets that grow what a
  press can land on without moving a pixel, and each host already had one place
  to put them.

  GTK widens `GtkWidget`'s `contains`, which is what `gtk_widget_pick` asks of
  each widget, so the slop applies wherever picking does: a press, a hover and a
  drop all go through one answer. AppKit widens the box at the top of
  `RnAppKitHitTest`, which recurses with each view's own coordinates, so each
  view's slop is applied to its own box on the way down. Neither host touches
  drawing, and both report `hit-slop=` in the tree dump, that being the only way
  to see a prop which by definition changes no pixels.

  The bound is the same on both and is the same as iOS's: a slop reaching outside
  the *parent* is only reachable where the parent is, because each host picks
  among the children of a view it is already inside. A slop that overlaps a
  sibling drawn on top loses to it, which is what stops an enlarged target
  stealing presses meant for something visible.

  Five tests per host, deliberately the same five in the same order over the same
  geometry, since the two hit tests are separate code answering one contract.
  Each edge is asserted on its own, four numbers in one struct being four chances
  to read one into the wrong side: sabotaging left into right fails two tests on
  each host and would pass any symmetric check. End to end, e2e/press.js gets a
  24pt square with 16 points of slop and the scenario taps twice in one run --
  28 points out, which must do nothing, and 12 points out, which must press it.
