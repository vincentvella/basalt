# Correctness gaps in what exists

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (3):**

1. ~~borderStyles, dashed and dotted borders~~ (GTK done; AppKit open)
2. pointScaleFactor, fractional scaling under Wayland
3. 3D transforms have no perspective: gsk_transform_perspective exists, and Trans

- ~~**`borderStyles`, dashed and dotted borders.**~~ Done on GTK 2026-10-07, with
  one limitation that is structural rather than unfinished.

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

  AppKit is not done. Its borders are layer properties, and a dashed layer border
  needs a `CAShapeLayer` rather than a `borderWidth`, which is a different shape
  of change.
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
