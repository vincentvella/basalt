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
- 3D transforms have no perspective: `gsk_transform_perspective` exists, and
  `Transform` carries the matrix, but nothing sets it up.
