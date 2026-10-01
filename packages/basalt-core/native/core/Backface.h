// Whether a transform has turned a view away from the viewer.
//
// `backfaceVisibility: 'hidden'` is what stops the back of a card being drawn
// mirrored halfway through a flip. Without it a rotation past ninety degrees
// shows the front face reversed, which reads as a rendering bug rather than a
// missing prop -- and all three hosts ignored the prop entirely.
//
// ## The test is the sign of the determinant
//
// A transform faces away exactly when it mirrors: the 2D part of the matrix
// has a negative determinant. `rotateY(180deg)` and `rotateX(180deg)` both
// produce one, `rotateZ(180deg)` does not -- turning something in its own
// plane never shows its back -- and `scaleX(-1)` does, which is right, because
// CSS hides that too.
//
// ## Why not CALayer's `doubleSided`
//
// macOS has this prop natively and the others do not. Using it there would be
// less code and would answer differently the day perspective lands: Core
// Animation would consider the whole 4x4 and the other two would consider the
// 2D part. One rule that all three obey is worth more than one host being
// idiomatic, and it is the same argument core/ScrollIndicator.h and
// core/ScrollBounds.h make. Nothing here draws perspective yet;
// docs/backlog/correctness.md records that.

// Header-only on purpose. `RnView.cpp` and `RnWin32View.cpp` are in the
// view-layer libraries, which link no React Native and no core -- that is what
// lets `demo_layout_gtk` and the view tests build without one. A .cpp here
// would put a link dependency between the two, the same reason
// core/ScrollIndicator.h keeps its constants inline.

#pragma once

namespace basalt {

// `matrix` is sixteen floats in CSS `matrix3d` order, which is what React
// Native produces and what every host here already stores.
//
// False for an identity or any rotation in the plane, so a view with no
// transform is never hidden by this.
// The rule from the four numbers that matter. A host that keeps only the 2D
// affine -- Windows does, because that is all its paint and hit testing need
// -- comes in here; everything else comes in below.
inline bool facesAway(float a, float b, float c, float d) {
  // A negative determinant means the transform mirrors, and a mirrored view is
  // one you are looking at the back of.
  return (a * d - b * c) < 0.0f;
}

inline bool facesAway(const float matrix[16]) {
  if (matrix == nullptr) {
    return false;
  }
  // The 2D part, in `matrix3d` order: m11 m12 are the first column and m21 m22
  // the second, so the four that matter are 0, 1, 4 and 5.
  return facesAway(matrix[0], matrix[1], matrix[4], matrix[5]);
}

} // namespace basalt
