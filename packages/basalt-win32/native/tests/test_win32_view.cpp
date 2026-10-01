// Tests for the Windows view layer.
//
// RnWin32View has no React Native dependency and no Direct2D dependency until
// something paints, so these need no Fabric machinery, no window and no
// display. They share the toolkit-free harness the GTK and macOS suites use.
//
// The questions are deliberately the same ones tests/test_view.cpp asks of GTK
// and tests/test_appkit_view.mm asks of AppKit, in the same order: what is being
// checked is largely whether three platforms still agree, so a divergence
// should fail here rather than in an app.

#include "TestHarness.h"

#include "RnWin32View.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using basalt::win32::RnWin32View;

namespace {

// The tests own their views, as the mounting registry will. A view owns neither
// its children nor its parent; see RnWin32View.h.
class Tree {
 public:
  RnWin32View *box(int32_t tag, float x, float y, float w, float h) {
    auto view = std::make_unique<RnWin32View>(tag);
    view->setFrame(x, y, w, h);
    RnWin32View *raw = view.get();
    views_.push_back(std::move(view));
    return raw;
  }

  // Releases one view, the way a Delete mutation would.
  void destroy(RnWin32View *view) {
    for (auto it = views_.begin(); it != views_.end(); ++it) {
      if (it->get() == view) {
        views_.erase(it);
        return;
      }
    }
  }

 private:
  std::vector<std::unique_ptr<RnWin32View>> views_;
};

int32_t tagOfChild(const RnWin32View *parent, size_t index) {
  return parent->children()[index]->tag();
}

} // namespace

TEST(win32_view_stores_its_tag_and_frame) {
  Tree tree;
  RnWin32View *view = tree.box(42, 12, 34, 100, 50);

  EXPECT_EQ(view->tag(), 42);
  EXPECT_NEAR(view->frame().x, 12.0, 0.001);
  EXPECT_NEAR(view->frame().y, 34.0, 0.001);
  EXPECT_NEAR(view->frame().width, 100.0, 0.001);
  EXPECT_NEAR(view->frame().height, 50.0, 0.001);
}

TEST(win32_children_insert_at_the_requested_index) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 200, 200);
  RnWin32View *first = tree.box(2, 0, 0, 10, 10);
  RnWin32View *second = tree.box(3, 0, 0, 10, 10);
  RnWin32View *between = tree.box(4, 0, 0, 10, 10);

  root->insertChild(first, 0);
  root->insertChild(second, 1);
  // An Insert's index counts positions in the parent's *final* child list, so
  // this lands between the two. An append-only implementation passes every
  // other test in this file.
  root->insertChild(between, 1);

  EXPECT_EQ(root->children().size(), size_t{3});
  EXPECT_EQ(tagOfChild(root, 0), 2);
  EXPECT_EQ(tagOfChild(root, 1), 4);
  EXPECT_EQ(tagOfChild(root, 2), 3);
}

TEST(win32_view_removes_only_its_own_child) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 200, 200);
  RnWin32View *mine = tree.box(2, 0, 0, 10, 10);
  RnWin32View *someone_elses = tree.box(3, 0, 0, 10, 10);
  RnWin32View *other = tree.box(4, 0, 0, 200, 200);

  root->insertChild(mine, 0);
  other->insertChild(someone_elses, 0);

  root->removeChild(someone_elses);

  EXPECT_EQ(root->children().size(), size_t{1});
  EXPECT_EQ(other->children().size(), size_t{1});
  EXPECT_EQ(someone_elses->parent()->tag(), 4);
}

TEST(win32_remove_detaches_but_does_not_destroy) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 200, 200);
  RnWin32View *child = tree.box(2, 5, 5, 10, 10);

  root->insertChild(child, 0);
  root->removeChild(child);

  // A reparent is a Remove and an Insert with no Delete between them, so the
  // view has to survive the gap with everything about it intact.
  EXPECT_EQ(root->children().size(), size_t{0});
  EXPECT(child->parent() == nullptr);
  EXPECT_NEAR(child->frame().x, 5.0, 0.001);
}

TEST(win32_insert_reparents_without_a_remove) {
  Tree tree;
  RnWin32View *first = tree.box(1, 0, 0, 200, 200);
  RnWin32View *second = tree.box(2, 0, 0, 200, 200);
  RnWin32View *child = tree.box(3, 0, 0, 10, 10);

  first->insertChild(child, 0);
  second->insertChild(child, 0);

  EXPECT_EQ(first->children().size(), size_t{0});
  EXPECT_EQ(second->children().size(), size_t{1});
  EXPECT_EQ(child->parent()->tag(), 2);
}

TEST(win32_destroying_a_view_leaves_nothing_pointing_at_it) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 200, 200);
  RnWin32View *child = tree.box(2, 0, 0, 10, 10);
  RnWin32View *grandchild = tree.box(3, 0, 0, 5, 5);

  root->insertChild(child, 0);
  child->insertChild(grandchild, 0);

  tree.destroy(child);

  // The registry owns every view, so a Delete of a parent does not delete its
  // children -- their own Deletes are separate mutations. What must not survive
  // is a pointer to the freed view, on either side.
  EXPECT_EQ(root->children().size(), size_t{0});
  EXPECT(grandchild->parent() == nullptr);
}

TEST(win32_z_index_reorders_painting_and_not_the_child_list) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 200, 200);
  RnWin32View *first = tree.box(2, 0, 0, 10, 10);
  RnWin32View *second = tree.box(3, 0, 0, 10, 10);

  root->insertChild(first, 0);
  root->insertChild(second, 1);
  first->setZIndex(10);

  // Fabric's Insert and Remove index into this list, so it stays in mutation
  // order no matter what zIndex says. That painting follows zIndex instead is
  // asserted in test_win32_paint.cpp, where it can be seen.
  EXPECT_EQ(tagOfChild(root, 0), 2);
  EXPECT_EQ(tagOfChild(root, 1), 3);
}

TEST(win32_scroll_offset_is_reported_in_the_tree) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 200, 200);
  root->setScrollOffset(0, 40);

  const std::string described = root->describeTree();
  EXPECT_EQ(described, std::string("view tag=1 frame=(0,0 200x200) scroll=(0,40)\n"));
}

TEST(win32_transform_reports_the_affine_part_of_matrix3d) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 200, 200);

  // A quarter turn with a translation, in CSS matrix3d order, which is column
  // major: elements 0, 1, 4, 5, 12 and 13 are the affine part. Getting the
  // indices wrong is a transpose, and a transpose is invisible on any
  // axis-aligned box -- which is why this asserts on an asymmetric matrix.
  const float matrix[16] = {
      0, 1, 0, 0, //
      -1, 0, 0, 0, //
      0, 0, 1, 0, //
      5, 7, 0, 1};
  root->setTransform(matrix);

  const std::string described = root->describeTree();
  EXPECT_EQ(described,
            std::string("view tag=1 frame=(0,0 200x200) transform=(0,1,-1,0,5,7)\n"));

  root->setTransform(nullptr);
  EXPECT_EQ(root->describeTree(), std::string("view tag=1 frame=(0,0 200x200)\n"));
}

TEST(win32_view_describes_its_tree_like_the_other_hosts) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 640, 420);
  root->setBackgroundColor(0.12f, 0.13f, 0.16f, 1.0f, true);

  RnWin32View *child = tree.box(2, 32, 32, 240, 160);
  child->setOpacity(0.4f);
  child->setClipsChildren(true);
  root->insertChild(child, 0);

  // Byte for byte what tests/test_appkit_view.mm expects of AppKit and what
  // gtk/RnView.cpp produces. scripts/compare_hosts.sh diffs these dumps line by
  // line, so this string is the contract rather than a convenience.
  const std::string expected =
      "view tag=1 frame=(0,0 640x420) bg=#1f2129ff\n"
      "  view tag=2 frame=(32,32 240x160) opacity=0.4 clip\n";
  EXPECT_EQ(root->describeTree(), expected);
}

int main(int argc, char **argv) {
  return basalt::testing::runAllTests(argc, argv);
}
