# Correctness gaps in what exists

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (3):**

1. ~~borderStyles, dashed and dotted borders~~
2. pointScaleFactor, fractional scaling under Wayland
3. 3D transforms have no perspective: gsk_transform_perspective exists, and Trans
4. Nine view style props that no host reads, and nothing said so

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

- **Nine view style props that no host reads, and nothing said so.** Counted
  2026-10-08 by going through `BaseViewProps` field by field and grepping all
  three hosts for each, after `cursor` and `borderStyle` both turned out to be
  props the backlog thought were handled. These are the ones no host mentions
  anywhere, and which nothing in this backlog mentioned either:

  - `boxShadow`, and the older `shadowColor`, `shadowOffset`, `shadowOpacity`
    and `shadowRadius` beside it. The most visible of the nine: a card, a menu
    and a dialog all read as flat without one. GSK has shadow nodes, CALayer has
    shadow properties, and Direct2D has a shadow effect, so every host has a
    primitive and none is wired to it.
  - `backgroundImage`, with `backgroundSize`, `backgroundPosition` and
    `backgroundRepeat`. This is where React Native put CSS gradients, so
    `linear-gradient(...)` in a style reaches the host and is dropped. GSK has
    linear, radial and conic gradient nodes; Core Graphics has `CGGradient`.
  - `filter`: the CSS filter functions on a view rather than on an image. Partly
    answerable with what the blur work already built: GSK has a blur node and a
    colour matrix, and Core Image has the rest.
  - `outlineColor`, `outlineWidth`, `outlineOffset` and `outlineStyle`. CSS's
    outline, which unlike a border takes no layout space and is drawn outside the
    box. The focus ring each host draws is the same idea and is hard-coded.
  - `mixBlendMode` and `isolation`.
  - `hitSlop`: a pressable's touch target extended past its frame. Less urgent
    with a cursor than with a thumb, but it is behaviour rather than decoration,
    so an app that relies on it is wrong rather than plain. See [input.md](input.md).
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
