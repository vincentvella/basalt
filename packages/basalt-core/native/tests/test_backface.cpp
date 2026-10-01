// Which transforms turn a view away from the viewer.
//
// One rule, shared, so that a card flipped on three desktops hides its back at
// the same angle. See core/Backface.h.

#include "TestHarness.h"

#include "Backface.h"

#include <sstream>

using basalt::facesAway;

namespace {

// CSS `matrix3d` order, which is what React Native produces.
struct Matrix {
  float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
};

} // namespace

TEST(backface_identity_faces_the_viewer) {
  const Matrix identity;
  EXPECT(!facesAway(identity.m));
}

// Turning something in its own plane never shows its back, however far it
// goes. This is the case a naive "is any angle past ninety" test gets wrong.
TEST(backface_a_z_rotation_never_turns_away) {
  Matrix half;
  half.m[0] = -1;
  half.m[5] = -1;
  EXPECT(!facesAway(half.m));
}

TEST(backface_a_half_turn_about_y_faces_away) {
  Matrix flipped;
  flipped.m[0] = -1;
  flipped.m[10] = -1;
  EXPECT(facesAway(flipped.m));
}

TEST(backface_a_half_turn_about_x_faces_away) {
  Matrix flipped;
  flipped.m[5] = -1;
  flipped.m[10] = -1;
  EXPECT(facesAway(flipped.m));
}

// A mirror is a back face too, which is what CSS says and what anyone flipping
// a card with a scale expects.
TEST(backface_a_mirror_faces_away) {
  Matrix mirrored;
  mirrored.m[0] = -1;
  EXPECT(facesAway(mirrored.m));
}

// Ninety degrees exactly is edge-on: the determinant is zero, nothing is
// visible either way, and the answer must not flicker. Not facing away is the
// side to fall on, because it is what the view was doing on the way in.
TEST(backface_edge_on_is_not_yet_away) {
  Matrix edge;
  edge.m[0] = 0;
  edge.m[10] = 0;
  EXPECT(!facesAway(edge.m));
}

TEST(backface_a_scale_and_rotation_together_still_answer) {
  // Rotated 180 about Y and scaled up: still mirrored, still away.
  Matrix both;
  both.m[0] = -2;
  both.m[5] = 2;
  EXPECT(facesAway(both.m));
}

TEST(backface_no_matrix_faces_the_viewer) {
  EXPECT(!facesAway(nullptr));
}
