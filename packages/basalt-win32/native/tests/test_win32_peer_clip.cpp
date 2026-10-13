// Which pixels of a `<TextInput>`'s EDIT are still the EDIT's.
//
// The peer is a child window, so USER32 draws it after everything Direct2D
// drew and the React tree has no say in it. `peerClip` is what works out the
// region that puts the say back -- see win32/Win32PeerClip.h -- and it is pure
// geometry over a view tree, so none of this needs a window, a control or
// React Native.
//
// The two failures it exists to prevent are both things a person sees and a
// test otherwise does not: a field in a half-scrolled list drawn over the
// list's neighbour, and a field floating at full brightness over a modal.

#include "TestHarness.h"

#include "RnWin32View.h"
#include "Win32PeerClip.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using basalt::win32::peerClip;
using basalt::win32::RnWin32View;

namespace {

class Tree {
 public:
  RnWin32View *box(float x, float y, float w, float h) {
    auto view = std::make_unique<RnWin32View>(nextTag_++);
    view->setFrame(x, y, w, h);
    RnWin32View *raw = view.get();
    views_.push_back(std::move(view));
    return raw;
  }

  // A box that paints: `alpha` is the background's, which is the whole of what
  // decides whether it covers a field under it.
  RnWin32View *painted(float x, float y, float w, float h, float alpha) {
    RnWin32View *view = box(x, y, w, h);
    view->setBackgroundColor(0.0f, 0.0f, 0.0f, alpha, true);
    return view;
  }

 private:
  int32_t nextTag_ = 1;
  std::vector<std::unique_ptr<RnWin32View>> views_;
};

std::string describe(const RECT &rect) {
  std::ostringstream out;
  out << rect.left << "," << rect.top << " " << rect.right << "," << rect.bottom;
  return out.str();
}

} // namespace

TEST(peer_clip_reports_the_field_where_the_tree_puts_it) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  RnWin32View *pane = tree.box(40, 20, 300, 400);
  RnWin32View *field = tree.box(10, 30, 200, 32);
  root->insertChild(pane, 0);
  pane->insertChild(field, 0);

  const auto clip = peerClip(field, root);
  EXPECT(clip.visible);
  // Offsets accumulate down the chain, and the root's own frame is not part of
  // them: these are client coordinates.
  EXPECT_EQ(describe(clip.bounds), std::string("50,50 250,82"));
  // Nothing clips and nothing paints over it, so the visible part is all of it.
  EXPECT_EQ(describe(clip.clip), std::string("50,50 250,82"));
  EXPECT_EQ(clip.occluders.size(), static_cast<size_t>(0));
}

TEST(peer_clip_narrows_a_field_half_scrolled_out_of_its_list) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  RnWin32View *list = tree.box(0, 100, 300, 200);
  list->setClipsChildren(true);
  RnWin32View *field = tree.box(0, 180, 300, 40);
  root->insertChild(list, 0);
  list->insertChild(field, 0);

  // Unscrolled the field is at y=280..320 and the list ends at y=300, so half
  // of it is outside. This is the case the old rule got wrong: it hid a field
  // only once it had left the list *entirely*, so the bottom 20 pixels went on
  // being drawn over whatever came after the list.
  const auto clip = peerClip(field, root);
  EXPECT(clip.visible);
  EXPECT_EQ(describe(clip.bounds), std::string("0,280 300,320"));
  EXPECT_EQ(describe(clip.clip), std::string("0,280 300,300"));

  // Scrolled until it is gone, and it is gone.
  list->setScrollOffset(0, 400);
  EXPECT(!peerClip(field, root).visible);

  // And scrolled back so it sits inside, the clip is the whole field again.
  list->setScrollOffset(0, 100);
  const auto inside = peerClip(field, root);
  EXPECT(inside.visible);
  EXPECT_EQ(describe(inside.clip), std::string("0,180 300,220"));
}

TEST(peer_clip_is_empty_for_a_field_nothing_can_see) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  RnWin32View *pane = tree.box(0, 0, 300, 400);
  RnWin32View *field = tree.box(0, 0, 200, 32);
  root->insertChild(pane, 0);
  pane->insertChild(field, 0);

  EXPECT(peerClip(field, root).visible);

  field->setHidden(true);
  EXPECT(!peerClip(field, root).visible);
  field->setHidden(false);

  // An ancestor hidden counts the same, which `display: none` on a pane is.
  pane->setHidden(true);
  EXPECT(!peerClip(field, root).visible);
  pane->setHidden(false);

  // Detached -- between a Remove and its Delete -- is not under the root, so
  // the walk never reaches it.
  pane->removeChild(field);
  EXPECT(!peerClip(field, root).visible);
}

TEST(peer_clip_cuts_away_what_the_tree_paints_over_a_field) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  RnWin32View *pane = tree.box(0, 0, 400, 600);
  RnWin32View *field = tree.box(20, 40, 200, 32);
  // The modal: a dimming backdrop over the whole window, rendered after the
  // panes because that is where an overlay goes in a React tree.
  RnWin32View *backdrop = tree.painted(0, 0, 800, 600, 0.63f);
  root->insertChild(pane, 0);
  pane->insertChild(field, 0);
  root->insertChild(backdrop, 1);

  const auto clip = peerClip(field, root);
  EXPECT(clip.visible);
  EXPECT_EQ(clip.occluders.size(), static_cast<size_t>(1));
  EXPECT_EQ(describe(clip.occluders[0]), std::string("0,0 800,600"));

  // A translucent backdrop counts. It has to: the field is a window and the
  // backdrop is paint, so there is no blend the two can take part in, and an
  // undimmed field over a modal is the bug this is here for.
  //
  // Fully transparent does not count -- that is a layout wrapper, which is
  // most of a real tree.
  backdrop->setBackgroundColor(0.0f, 0.0f, 0.0f, 0.0f, true);
  EXPECT_EQ(peerClip(field, root).occluders.size(), static_cast<size_t>(0));

  // Neither does no background at all, which is not the same thing.
  backdrop->setBackgroundColor(0.0f, 0.0f, 0.0f, 1.0f, false);
  EXPECT_EQ(peerClip(field, root).occluders.size(), static_cast<size_t>(0));

  // Nor an invisible one, however solid its colour.
  backdrop->setBackgroundColor(0.0f, 0.0f, 0.0f, 1.0f, true);
  backdrop->setOpacity(0.0f);
  EXPECT_EQ(peerClip(field, root).occluders.size(), static_cast<size_t>(0));
}

TEST(peer_clip_ignores_everything_painted_before_the_field) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  // A solid pane behind the field, and the field's own background: both are
  // painted first, and a field is not hidden by what is under it.
  RnWin32View *behind = tree.painted(0, 0, 800, 600, 1.0f);
  RnWin32View *pane = tree.painted(0, 0, 400, 600, 1.0f);
  RnWin32View *field = tree.box(20, 40, 200, 32);
  root->insertChild(behind, 0);
  root->insertChild(pane, 1);
  pane->insertChild(field, 0);

  const auto clip = peerClip(field, root);
  EXPECT(clip.visible);
  EXPECT_EQ(clip.occluders.size(), static_cast<size_t>(0));
}

TEST(peer_clip_follows_z_index_rather_than_the_child_list) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  RnWin32View *field = tree.box(0, 0, 200, 32);
  RnWin32View *sheet = tree.painted(0, 0, 800, 600, 1.0f);
  // The sheet is inserted first, so by mutation order it is painted first and
  // covers nothing.
  root->insertChild(sheet, 0);
  root->insertChild(field, 1);
  EXPECT_EQ(peerClip(field, root).occluders.size(), static_cast<size_t>(0));

  // zIndex restacks painting without touching the child list, and the answer
  // has to follow the painting -- which is the same rule hit testing is under.
  sheet->setZIndex(40);
  EXPECT_EQ(peerClip(field, root).occluders.size(), static_cast<size_t>(1));
}

TEST(peer_clip_narrows_an_occluder_to_its_own_clipping_ancestor) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  RnWin32View *field = tree.box(0, 0, 400, 200);
  // A panel after the field whose content is twice its height and clipped to
  // it. What covers the field is the panel's visible part, not the child's
  // full box -- the half outside the panel is not drawn anywhere.
  RnWin32View *panel = tree.box(0, 0, 400, 100);
  panel->setClipsChildren(true);
  RnWin32View *content = tree.painted(0, 0, 400, 200, 1.0f);
  root->insertChild(field, 0);
  root->insertChild(panel, 1);
  panel->insertChild(content, 0);

  const auto clip = peerClip(field, root);
  EXPECT_EQ(clip.occluders.size(), static_cast<size_t>(1));
  EXPECT_EQ(describe(clip.occluders[0]), std::string("0,0 400,100"));
}

TEST(peer_clip_leaves_a_transformed_view_alone) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  RnWin32View *field = tree.box(0, 0, 200, 32);
  RnWin32View *card = tree.painted(0, 0, 800, 600, 1.0f);
  root->insertChild(field, 0);
  root->insertChild(card, 1);
  EXPECT_EQ(peerClip(field, root).occluders.size(), static_cast<size_t>(1));

  // Rotated, its box is no longer a rectangle in these coordinates, and a
  // region is rectangles. Guessing at a bounding box would hide more of the
  // field than the card covers, so nothing is cut away at all -- the same
  // decision the placement already makes about a transformed field.
  const float rotated[16] = {0.0f,
                             1.0f,
                             0.0f,
                             0.0f,
                             -1.0f,
                             0.0f,
                             0.0f,
                             0.0f,
                             0.0f,
                             0.0f,
                             1.0f,
                             0.0f,
                             0.0f,
                             0.0f,
                             0.0f,
                             1.0f};
  card->setTransform(rotated);
  EXPECT_EQ(peerClip(field, root).occluders.size(), static_cast<size_t>(0));
}

TEST(peer_clip_does_not_treat_the_fields_own_children_as_over_it) {
  Tree tree;
  RnWin32View *root = tree.box(0, 0, 800, 600);
  RnWin32View *field = tree.box(0, 0, 200, 32);
  RnWin32View *inside = tree.painted(0, 0, 200, 32, 1.0f);
  root->insertChild(field, 0);
  field->insertChild(inside, 0);

  // Inside the field is not over it, and a field that cut itself away for its
  // own content would never be visible at all.
  const auto clip = peerClip(field, root);
  EXPECT(clip.visible);
  EXPECT_EQ(clip.occluders.size(), static_cast<size_t>(0));
}
