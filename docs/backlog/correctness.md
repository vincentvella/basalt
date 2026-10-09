# Correctness gaps in what exists

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (3):**

1. ~~borderStyles, dashed and dotted borders~~
2. pointScaleFactor, fractional scaling under Wayland
3. 3D transforms have no perspective: gsk_transform_perspective exists, and Trans
4. ~~Five view style props that no host reads, and nothing said so~~: seven of
   the nine done on GTK and AppKit, two written down as deliberately ignored.
   What is left is entry 6
5. ~~A type check used as a liveness check~~, fixed in four places; the ordering
   it depended on is now core's
6. backgroundSize, backgroundPosition and backgroundRepeat are ignored, and they
   apply to the gradients that are implemented

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

- **Five view style props that no host reads, and nothing said so.** All five
  are now either done or written down; what is left of them is the
  `backgroundSize` entry below. Counted
  2026-10-08 by going through `BaseViewProps` field by field and grepping all
  three hosts for each, after `cursor` and `borderStyle` both turned out to be
  props the backlog thought were handled. These are the ones no host mentions
  anywhere, and which nothing in this backlog mentioned either:

  - ~~`boxShadow`~~, **done on GTK and AppKit 2026-10-08**, and with it the
    older `shadowColor`, `shadowOffset`, `shadowOpacity` and `shadowRadius`,
    which are iOS's pre-CSS spelling of the same idea and are now converted into
    it. What the work turned out to be is at the end of this entry.
  - `backgroundImage`: **gradients are done on GTK and AppKit 2026-10-08**,
    linear and radial both. `backgroundSize`, `backgroundPosition` and
    `backgroundRepeat` are still ignored, and that is now its own entry below
    rather than a line here: they are not waiting on an image loader, which is
    what this entry used to say.
  - ~~`shouldRasterize` and `removeClippedSubviews`~~: **both deliberately not
    implemented, written down 2026-10-08** with the measurements that decide it.
    At the end of this entry.
  - ~~`filter`~~, **done on GTK and AppKit 2026-10-08**, all nine functions.
    What the work turned out to be is at the end of this entry.
  - ~~`outlineColor`, `outlineWidth`, `outlineOffset` and `outlineStyle`~~,
    **done on GTK and AppKit 2026-10-08**. What the work turned out to be is at
    the end of this entry.
  - ~~`mixBlendMode`~~, **done on GTK and AppKit 2026-10-08**; `isolation`
    beside it is deliberately not implemented, and both are at the end of this
    entry.
  - ~~`hitSlop`~~, **done on GTK and AppKit 2026-10-08**. The one of the nine
    that is behaviour rather than decoration, so an app relying on it was wrong
    rather than plain. What the work turned out to be is at the end of this
    entry.
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

  **Radial gradients, done on GTK and AppKit 2026-10-08.** The ending shape is
  the whole of it: CSS gives six ways to size one and four corners to measure to,
  and every wrong answer still draws a radial gradient. So the geometry is ported
  rather than written, from React Native's own `RCTRadialGradient.mm`, and shared
  in `core/Gradients.h` beside the linear construction -- `closest-side` and
  `farthest-side` per axis, a circle taking the smaller or larger of the two, and
  the two corner keywords sized to meet the corner with the aspect ratio of the
  matching side shape. That last rule is the one a reimplementation gets wrong:
  the naive answer, the corner's own dx and dy as the radii, is smaller and looks
  like a gradient. Both hosts assert the corner lies on the ellipse.

  Stops resolve against the longer radius, which is React Native's choice rather
  than one made here, so a stop at 10pt is the same distance on all three
  platforms.

  **One list, not two.** `background-image` is a single list that can hold both
  kinds and paints the first on top, so the view layers took a tagged record --
  `RnGradient` on GTK, `RnAppKitGradient` on AppKit -- rather than a second
  setter, and a test on each host puts a radial and a linear one in one list and
  asserts the order between them. Two lists would have lost it.

  GTK is a direct mapping: `gtk_snapshot_append_radial_gradient` takes a centre
  and two radii, so an ellipse needs no special case. AppKit is not, and that is
  the one piece of real work on that side: `CGContextDrawRadialGradient` draws
  between two *circles*, so an ellipse is a scaled coordinate system, scaled
  about the centre so the centre does not move. Scaling about the origin instead
  draws an ellipse of the right shape in the wrong place, which is exactly what
  the first attempt did and what the test that assumes each radius acts on its own
  axis caught.

  Eight tests in core against the spec's numbers, four on GTK's render tree and
  three on its pixels, four on AppKit's pixels, and a scenario on both hosts
  reading the resolved shape out of the dump: `circle at 30% 30%` on a 60x40 box
  is a radius of hypot(42, 28), and the failure message says what the closest
  corner, the farthest side and a defaulted centre would each report instead.

  **What is left of the prop.** `backgroundSize`, `backgroundPosition` and
  `backgroundRepeat` have nothing to act on until an image can be a background,
  which is a loader question rather than a drawing one.

  **Windows** has `ID2D1RenderTarget::CreateRadialGradientBrush`, whose
  `D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES` carries a centre and two radii
  directly, so the ellipse needs no transform there and the shared geometry hands
  it the numbers. Its scenario skips by name with the linear one.

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

  **`filter`, done on GTK and AppKit 2026-10-08.** Nine CSS functions, of which
  seven are affine maps of colour: `brightness`, `contrast`, `grayscale`,
  `hueRotate`, `invert`, `saturate` and `sepia`. The Filter Effects spec gives
  every matrix exactly, so the arithmetic is shared in `core/Filters.h` and the
  hosts only hand it over.

  Composing those seven is multiplying their matrices, so a list is **one** node
  rather than a stack -- and because each is a per-pixel affine map, it does not
  matter whether the blur happens before or after: a blur is a weighted average
  whose weights sum to one, and an affine map commutes with that. `opacity()`
  multiplies the view's own opacity, which is cheaper than a filter pass on both
  hosts. Two `blur()`s compose in quadrature, 6 and 8 making 10, because
  convolving two gaussians gives the combined variance.

  `dropShadow()` is the exception: it depends on the alpha silhouette at its
  point in the chain, so folding it in with the rest would be wrong rather than
  approximate. **It was reported and not applied until later the same day; it is
  drawn now, and what it needed is at the end of this entry.**

  **GTK is checked against a rendered pixel, which is new here.** GSK's
  colour-matrix node applies `transpose(matrix) * pixel + offset` to
  unpremultiplied colour, and a transposed matrix changes every channel quietly:
  a node-counting test passes and the colours are wrong. So the GTK suite gained
  a twenty-line `renderedPixel` helper -- `gsk_renderer_realize` takes a NULL
  surface, so a node tree can be rasterised with no window at all, preferring
  the GL renderer a real app uses and falling back to cairo -- and the test
  asserts that grayscale(1) of (1, 0.5, 0) comes out 145 in all three channels.
  Sabotaging the transpose gives 81, 255, 28. That helper is there for the next
  prop that needs a colour rather than a node.

  **AppKit is `CALayer.filters`, which is one of the few places macOS does better
  than React Native's own iOS half**: Core Image filters on a layer are public
  API here and private there, so iOS builds a SwiftUI wrapper and a
  multiply-blend layer to approximate what this sets directly. `renderInContext:`
  draws no filters, as it draws no shadows, so the tests assert what each
  `CIFilter` was handed -- the matrix row by row, which is what a column-wise
  hand-off would get wrong.

  Eleven tests in core against the spec's numbers, four on GTK, five on AppKit,
  and a scenario that reads the matrix's effect on a probe colour out of the dump
  on both hosts: `grayscale(1) brightness(1.2)` of (1, 0.5, 0.25) is 0.588 * 1.2,
  which is #b4b4b4, and the failure message says what #969696 and #a4a4a4 would
  mean. The AppKit test for taking a filter away found a real bug on the way: a
  list of nothing but `opacity()` leaves no Core Image filter behind, so the
  early return left the view at a quarter of its opacity for good.

  **The `outline` family, done on GTK and AppKit 2026-10-08.** Four props and one
  ring: CSS gives an outline one width, one colour, one offset and one style for
  the whole box, where a border has four of each. It is drawn outside the box and
  takes no layout space, so nothing about it touches the frame, which is also why
  it is invisible to every assertion this project already had about a view.

  **Nothing clips it, and that is the decision in both implementations.** CSS
  does not clip an element's own outline for `overflow: 'hidden'`, so a ring
  drawn as a child of the view it belongs to would vanish on exactly the views
  that most often carry one. GTK gets this for free: the outline is appended to
  the snapshot after the children's clip has been popped. AppKit does not, so the
  ring follows the box shadows' rule and moves into the parent's layer when the
  view clips its own layer, which happens both for `overflow: 'hidden'` and for
  radii too elliptical for `cornerRadius`.

  **Stroked rather than bordered, on both hosts, for the same reason.** A stroke
  takes a dash pattern, so dotted and dashed need no second mechanism, and it
  takes an arbitrary path, so elliptical radii need no special case. GTK keeps
  `gtk_snapshot_append_border` for the solid case, which is cheaper, and strokes
  the other two; AppKit strokes a `CAShapeLayer` throughout. A stroke straddles
  its path, so the path is offset plus half the width out, where the border node
  sits inside a rect grown by offset plus width. The two spellings put the ring
  in the same place, which is what the two hosts' tests assert in their own
  terms.

  The radii grow with the ring so it stays concentric: each non-zero radius gains
  what the ring moved out, and a corner that was square stays square, which is
  what React Native's iOS half does. A square ring around a rounded card is the
  wrong answer that looks almost right, so both hosts have a test that asserts
  the corner specifically -- a pixel outside the rounded ring on GTK, and
  `CGPathContainsPoint` at the corner on AppKit.

  Five pixel tests on GTK, seven layer tests on AppKit, and one scenario reading
  all four numbers out of the dump on both hosts: `outline=(3,2,#e0484dff,dashed)`
  from a 3pt dashed ring 2pt out, with the failure message saying what a dropped
  offset, a dropped style and an unresolved colour each look like. Sabotage: the
  ring's growth, its placement and its radii each fail their own tests, and the
  scenario fails on both hosts when the mounting manager stops passing the
  offset.

  **Windows** needs `ID2D1RenderTarget::DrawRoundedRectangle` on a rect grown by
  the offset plus half the width, with an `ID2D1StrokeStyle` built from
  `D2D1::StrokeStyleProperties` carrying `D2D1_DASH_STYLE_CUSTOM` and the same
  dash arrays the other two hosts use, drawn after the children for the clipping
  reason above. Its scenario skips by name.

  **`mixBlendMode`, done on GTK and AppKit 2026-10-08.** Seventeen CSS blend
  modes, of which `normal` is "do nothing". The keyword crosses the seam, as
  `cursor` does and for the same reason: GSK's blend modes are CSS's, Core
  Image's blend filters are CSS's, and `core/BlendModes.h` is the one place that
  says what a value is called. Its test is a round trip through React Native's
  own `blendModeFromString`, so nothing in the table agrees with itself by
  construction.

  **AppKit is one property, and it is public here.**
  `CALayer.compositingFilter` takes a Core Image filter and blends the layer with
  what is beneath it, so the implementation is a sixteen-entry table from keyword
  to `CI<Name>BlendMode`. React Native's own iOS half sets the same property to a
  bare string, which is undocumented there; this sets the `CIFilter` the property
  is documented to take. Every one of the sixteen filters exists, which the test
  measures with `filterWithName:` rather than trusting the list -- including
  `CILinearDodgeBlendMode` for `plus-lighter`, clamped addition being what that
  keyword means.

  **GTK is the whole shape of the feature, because GSK's blend node takes its two
  children after the push.** A blend needs the backdrop, and a view cannot see
  its own backdrop: it is the parent that has to build the node. So the parent's
  snapshot looks ahead at its children, pushes one blend per blended child before
  it paints anything -- last child outermost, so the pushes nest the way the
  blends do -- and pops each one as its child is reached. The first blended child
  then has this view's own content as its backdrop, the second has that blend's
  result plus whatever came between them, and so on.

  Two things fall out of that and are asserted on their own. The pushes have to
  be in paint order rather than insertion order, because zIndex can differ from
  it, and a stable sort of the blended ones gives the same order as the sort of
  the whole list. And `overflow: 'hidden'` can no longer be one clip around every
  child, because the pops that close each blend happen between the children and
  that clip is what they would close; each child gets its own clip instead, which
  paints the same picture since clipping a group and clipping each of its members
  to the same box are the same thing.

  **The deviation, which is the one thing here that is not CSS.** CSS blends with
  the backdrop of the nearest stacking context, which for a plain `<View>` reaches
  past its parent. GTK's backdrop stops at the parent, because that is what the
  parent's snapshot can see; AppKit's does not, because Core Animation composites
  the whole layer tree. So a blended view over a *grandparent's* background is two
  different pictures on the two hosts, and the e2e scenario is deliberately an
  arrangement where they agree: a blended child over its own parent's opaque
  background. The dump carries the keyword, which is what both hosts can be asked
  about.

  `plus-lighter` is the one mode GSK has no node for. The keyword is stored and
  reported either way -- the dump says what the app asked for, which is the rule
  `cursor` follows for a name no theme has -- and the view paints unblended rather
  than wrongly blended, which a test measures rather than assumes.

  Nine pixel tests on GTK, five layer tests on AppKit, three on the shared table,
  and a scenario on both hosts. Sabotage: the keyword table, the blend itself, the
  paint-order sort and each host's wiring all fail tests of their own.

  **Windows** has `CLSID_D2D1Blend`, whose `D2D1_BLEND_PROP_MODE` is CSS's list
  plus a few Direct2D extras, so the arithmetic is the platform's. What it needs
  is the backdrop as an input: the effect takes two bitmaps, so the parent has to
  render what is beneath the blended child into an intermediate
  `ID2D1BitmapRenderTarget` rather than straight to the window, which is the same
  look-ahead the GTK half does and the reason this is not a one-line port. Its
  scenario skips by name.

  **`isolation` is deliberately not implemented, on any host.** It asks for an
  element to become a stacking context so that its descendants' blending stops
  there. On GTK that is already true of every view, the backdrop stopping at the
  parent, so `isolate` is satisfied and `auto` is the deviation above -- there is
  nothing to write. On AppKit there is no public way to say "composite this
  subtree as a group": the levers that happen to do it are `shouldRasterize`,
  which fixes the subtree to one scale and costs quality, and a mask or filter
  on the parent, which changes what it draws. React Native's own iOS half ignores
  the prop too. Whatever is written here would be a second deviation rather than
  the feature, so what is recorded is the reason.

  **The four iOS shadow props, done on GTK and AppKit 2026-10-08**, by becoming
  the CSS shadow they describe: `core/LegacyShadow.h` converts them and both
  mounting managers hand the combined list to the box shadow path they already
  had, so there is one shadow mechanism from the manager down.

  **That these hosts honour them at all is a decision, not a detail.** The props
  are iOS-only: Android's view config does not carry them, so on Android they do
  nothing and `elevation` is used instead. They are honoured here for two
  reasons. The AppKit host is macOS, where react-native-macos honours them for
  the same reason its iOS parent does. And a cross-platform app that asks for a
  shadow means the shadow, so dropping it on a desktop that can draw one would be
  a deliberate difference from what the app's author saw. The alternative --
  ignoring them, as Android does -- is a defensible reading of "iOS-only" and is
  what to come back to if this turns out to surprise anybody.

  **Two of the conversions are not identity, and they are the whole reason this
  is shared code.** `shadowRadius` is a `CALayer` blur radius, which is a
  gaussian standard deviation, and CSS's blur-radius is twice one -- so it
  doubles. `shadowOpacity` multiplies the colour's own alpha, as
  `CALayer.shadowOpacity` does on top of `shadowColor`. Each host doubling a
  radius on its own is exactly the arithmetic that comes out half as soft on one
  platform and goes unnoticed; so is each host deciding on its own what an
  opacity of zero means. Both props keep React Native's defaults, offset (0, -3)
  and radius 3, so a view setting only a colour and an opacity gets the shadow
  iOS would give it.

  Both mechanisms can be set at once, which iOS allows, and the legacy shadow
  goes at the back of the list: adding a `boxShadow` to an old component then
  cannot be hidden behind the shadow it already had. Six tests in core against
  the conversions and the gates, and a scenario on both hosts reading
  `shadow=(2,4,6,0,#00000080)` out of the dump -- a radius passed through
  unchanged reports a 3, and a dropped opacity reports `#000000ff`.

  **Windows** needs nothing new for these beyond `CLSID_D2D1Shadow`, which its
  `boxShadow` entry already names: the conversion is shared, so the legacy props
  arrive as ordinary shadows in the same list.

  **`dropShadow()`, done on GTK and AppKit 2026-10-08**, which completes
  `filter`. It is the one function of the nine that is a shadow rather than a
  colour map: it shadows the subtree's *alpha*, not its box, which is both why it
  could not join the collapsed matrix and the one thing that tells it from
  `boxShadow` in a picture. The GTK test asserts exactly that, with a frame made
  of four bars: the hole in the middle of the view is a hole in its shadow, which
  no box shadow can do.

  **The blur is the number that needed care, and it was measured.** React Native
  parses the third length into a field it calls `standardDeviation` and hands it
  over -- its own iOS half passes it straight to SwiftUI's `.shadow(radius:)` --
  and the two hosts want different things with it. `CALayer.shadowRadius` is a
  standard deviation, so AppKit takes it unchanged. GSK's shadow radius is CSS's,
  which is twice one, so GTK doubles it. That claim is not read off a document: a
  pixel test renders a shadow of radius R beside `gtk_snapshot_push_blur` of
  radius R and compares the alpha falloff at three points, because the sigma of
  the blur node was measured here earlier. Equal profiles mean equal
  conventions, and a factor of two moves them by 60 units of alpha or more.

  Both dumps print the standard deviation rather than either platform's radius,
  so the cross-host diff compares what React Native said rather than what each
  toolkit was told, and the scenario fails with a 6 or a 1.5 when a conversion
  lands in the wrong place.

  **Two limits, recorded rather than approximated.** A `CALayer` has one shadow,
  so AppKit draws the first of a list and logs once; GTK's node takes the whole
  array. And the interleaving with the colour matrix is gone: CSS applies the
  list left to right, so `drop-shadow(...) grayscale(1)` greys the shadow and
  `grayscale(1) drop-shadow(...)` does not, while a collapsed matrix gives one
  order. The shadows go on outermost, which is the second of those -- the usual
  authoring order -- and keeping both would mean emitting the list as a sequence
  of stages rather than a resolved triple.

  **A bug of the same shape as one already fixed here.** Taking the filters off
  left the shadow behind, because the early return in `setRnFilters:` tested only
  the Core Image list and the opacity -- and a list of nothing but
  `dropShadow()` leaves no Core Image filter. That is exactly what the
  `opacity()` early return did before it was caught, found again by writing the
  same test for the new half, which is the argument for writing it rather than
  assuming the pattern held.

  **Windows** has `CLSID_D2D1Shadow`, which takes a blurred alpha mask of what is
  drawn -- which is what this function is -- so it needs the effect graph its
  `filter` entry already names, with the shadow applied to the layer's output and
  composited beneath it. Its scenario skips by name.

  **`shouldRasterize` and `removeClippedSubviews`: deliberately not implemented,
  and the reasons are not the same one.** Both are performance hints, which is
  where the resemblance stops.

  **`shouldRasterize` cannot arrive at all, which took one grep to find and is
  the whole answer.** The C++ prop is read from the raw prop
  `shouldRasterizeIOS`, and only `BaseViewConfig.ios.js` declares that in
  `validAttributes`. This platform's view config override takes *Android's*
  config -- `packages/basalt-core/src/overrides/BaseViewConfig.ts`, for the
  reasons that file gives -- and Android's declares `removeClippedSubviews` and
  not `shouldRasterizeIOS`. So React never sends it, `BaseViewProps::shouldRasterize`
  is false in every host for every view, and a host that read it would be reading
  a constant. Re-checkable in seconds: grep the two config files for the two
  names.

  Making it arrive is one line in that override. Then AppKit is two more, and
  they are React Native's own: `layer.shouldRasterize = prop` with
  `layer.rasterizationScale` set to the backing scale when on and 1 when off,
  which is exactly `RCTViewComponentView.mm`'s. GTK has no node for it -- the
  node types in GTK 4.22's `gskenums.h` have no cache, measured by reading them
  -- so the nearest thing is rendering the subtree with
  `gsk_renderer_render_texture` and appending a texture node, which is a cache
  with an invalidation problem: nothing would know when the subtree changed.

  Not done, and the reason is the shape of the prop rather than the cost. A hint
  that is wrong is slower and blurrier than no hint, so a half-cache on one host
  is worse than the honest nothing both hosts do now. If it is ever wanted, the
  one-line view config change is what makes it testable at all.

  **`removeClippedSubviews` does arrive and is ignored.** Android declares it, so
  it reaches both hosts' props and neither reads it. Ignoring it is correct here
  for a reason that is upstream's own: React Native disables the prop entirely
  when its view-culling feature flag is on -- `if
  (!ReactNativeFeatureFlags::enableViewCulling())` guards the only place iOS
  reads it -- so the direction of travel is culling in the renderer, which
  decides what to mount for every view rather than per prop.

  Honouring it means what iOS does: hold the React children in a side array,
  attach only the ones inside a clip rect, and re-attach the rest when the prop
  goes off. That is the same machinery culling needs, built per-view and worse,
  and it has the failure iOS is known for -- a clipped child loses anything the
  platform was holding for it, which on a desktop includes focus and a text
  field's selection. `backlog/scrollview.md` carries the culling entry it
  belongs with: `disableViewCulling` is never set, and that is the knob to reach
  for first.

  So neither is a gap to close. What they were, until this was written, is two
  props nothing said anything about, which is the thing this entry exists to
  stop.

- **`backgroundSize`, `backgroundPosition` and `backgroundRepeat` are ignored,
  and the previous note about them was wrong.** It said they "have nothing to act
  on until an image can be a background, which is a loader question rather than a
  drawing one". That is false twice over: CSS treats a gradient as an image, so
  all three apply to the gradients both hosts now draw, and React Native's iOS
  half already applies them to exactly those. Written down 2026-10-08 after
  reading that implementation rather than the prop names.

  **What iOS does**, in `RCTViewComponentView.mm`'s background-image block: it
  takes the *padding* frame as the positioning area and the layer bounds as the
  painting area -- `background-origin: padding-box` and `background-clip:
  border-box`, which it says in a comment -- computes an image size from
  `backgroundSize` and `backgroundRepeat` through
  `RCTBackgroundImageUtils.calculateBackgroundImageSize`, renders the gradient at
  *that* size rather than the view's, and then positions and tiles it. The three
  lists are indexed with `imageIndex % list.size()`, which is CSS's rule for a
  list shorter than the image list.

  **What that changes here is not small.** Both hosts resolve a gradient against
  the view's border box: `GtkMountingManager` and `AppKitMountingManager` pass
  `layoutMetrics.frame.size` into `core/Gradients.h`, which is what decides the
  gradient line and the ending shape. Honouring these three means resolving
  against the background image's size instead, positioning that image in the
  padding box, and repeating it -- so the shared resolution grows an input and
  the two view layers grow a tile. A view with padding is already a visible
  difference from iOS for the same stylesheet, which nothing has noticed because
  nothing in `e2e/views.tsx` puts a gradient on a padded view.

  **Both hosts have the pieces.** GTK has `gtk_snapshot_push_repeat`, which the
  image tiling already uses, and `GSK_REPEATING_LINEAR_GRADIENT_NODE` and
  `GSK_REPEATING_RADIAL_GRADIENT_NODE` for the cases where the repeat is along
  the gradient itself. AppKit draws its gradients in `drawRect:`, so a sized
  sub-rect and a loop is the whole of it, or a `CGPattern` for the tiling case.
  Windows would need the same geometry with
  `ID2D1BitmapBrush`'s extend modes.

  The sizing is the specified part and belongs in `core/Gradients.h` beside the
  rest: `cover` and `contain` against an intrinsic size a gradient does not have
  (CSS says a gradient's intrinsic size is the positioning area, which is why
  iOS passes the area in as `itemIntrinsicSize`), lengths and percentages, and
  the four repeat keywords, of which `space` and `round` change the tile size
  rather than only the step.

- ~~**A type check used as a liveness check.**~~ Found and fixed 2026-10-08, by
  accident, which is the part worth writing down.

  Four places asked `RN_IS_VIEW(view)` to decide whether a borrowed view pointer
  was still usable during teardown: `~GtkFocusManager`,
  `GtkScrollViewManager::remove`, `stopMomentum` and
  `GtkTextInputManager::remove`. A type check cannot answer that question. It
  reads the instance's type pointer out of the object, so on a freed GObject it
  reads GLib's 0xaa poison and dereferences it -- the check is the crash.

  **How it surfaced.** Adding four unrelated fields to RnView's struct, for the
  `outline` prop, moved what the freed bytes at that offset happened to be, and
  the GTK suite started exiting 139 *after* printing "ok" for a passing test.
  The suite's own output said only "exit 139"; installing the host's crash
  handler in `TestMainGtk.cpp` said `SIGSEGV at 0xaaaaaaaaaaaaaaaa`, three frames
  under `RN_IS_VIEW`, from the focus manager's destructor. That handler stays.

  **Two fixes, and the second is the one that matters.** The focus manager holds
  its root with `g_object_add_weak_pointer` now, which is what the key controller
  beside it already did, so the pointer nulls itself on finalise and every path
  that reads the root tolerates its absence. And `core/MountingWalk.h` now calls
  `forgetTag` *before* `destroyView`, in the Delete path and in `releaseAllViews`:
  every per-tag side table is told to let go while the view is still alive, which
  is the ordering a scroll controller, a text peer and a canvas all need. That one
  line removes the hazard class for all three hosts rather than patching the
  symptom in each.

  The three remaining checks are plain null checks now, with the contract named
  at each, because leaving an idiom in place that looks like it works is how the
  next person comes to rely on it.

  `focus_a_manager_survives_its_root_being_destroyed` asserts the contract. It
  deliberately does not claim to reproduce the crash: whether those freed bytes
  fault depends on what the heap did beforehand, so it passes with the old check
  when run alone. The evidence is the full suite, which crashed in three runs out
  of three with the struct change and none after.
