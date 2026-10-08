# Correctness gaps in what exists

Part of the [backlog](../BACKLOG.md). Not scheduled.

**Open (3):**

1. borderStyles, dashed and dotted borders
2. pointScaleFactor, fractional scaling under Wayland
3. 3D transforms have no perspective: gsk_transform_perspective exists, and Trans

- `borderStyles`, dashed and dotted borders. GTK's border node paints solid
  only, so these need a custom path.
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
