// The geometry of a hidden title bar: how big the caption is, which button a
// point is over, and whether a point drags the window.
//
// Pure functions of a view tree and a size, so no window, no DWM and no React
// Native. These are the questions a custom caption gets wrong in ways a person
// notices at once -- a close button a few pixels off, a label that will not
// drag, a button that drags instead of pressing -- and a test does not, unless
// it asks them.

#include "TestHarness.h"

#include "RnWin32View.h"
#include "Win32TitleBarLayout.h"

#include <memory>
#include <sstream>
#include <string>
#include <vector>

using basalt::win32::CaptionButton;
using basalt::win32::captionButtonAt;
using basalt::win32::captionMetricsForDpi;
using basalt::win32::isTitleBarDragRegionAt;
using basalt::win32::kTitleBarDragRegionId;
using basalt::win32::kTitleBarNoDragRegionId;
using basalt::win32::RnWin32View;

namespace {

class Tree {
 public:
  RnWin32View *box(int32_t tag, float x, float y, float w, float h, const char *nativeId = "") {
    auto view = std::make_unique<RnWin32View>(tag);
    view->setFrame(x, y, w, h);
    view->setNativeId(nativeId);
    RnWin32View *raw = view.get();
    views_.push_back(std::move(view));
    return raw;
  }

 private:
  std::vector<std::unique_ptr<RnWin32View>> views_;
};

} // namespace

TEST(caption_metrics_are_windows_elevens_and_scale_with_dpi) {
  const auto at100 = captionMetricsForDpi(96);
  EXPECT_NEAR(at100.height, 32.0f, 0.01f);
  EXPECT_NEAR(at100.buttonWidth, 46.0f, 0.01f);

  const auto at150 = captionMetricsForDpi(144);
  EXPECT_NEAR(at150.height, 48.0f, 0.01f);
  EXPECT_NEAR(at150.buttonWidth, 69.0f, 0.01f);

  // A DPI of zero is what a window that has not been shown can report; it is
  // treated as 96 rather than collapsing the caption to nothing.
  EXPECT_NEAR(captionMetricsForDpi(0).height, 32.0f, 0.01f);
}

TEST(caption_buttons_sit_flush_right_in_the_order_windows_puts_them) {
  const auto metrics = captionMetricsForDpi(96);
  const float width = 800;

  EXPECT(captionButtonAt(799, 10, width, metrics) == CaptionButton::Close);
  EXPECT(captionButtonAt(width - 46 - 1, 10, width, metrics) == CaptionButton::Maximize);
  EXPECT(captionButtonAt(width - 92 - 1, 10, width, metrics) == CaptionButton::Minimize);
  EXPECT(captionButtonAt(width - 138 - 1, 10, width, metrics) == CaptionButton::None);

  // Below the caption, or outside the window, is no button at all.
  EXPECT(captionButtonAt(799, 32, width, metrics) == CaptionButton::None);
  EXPECT(captionButtonAt(-1, 10, width, metrics) == CaptionButton::None);
}

TEST(a_label_inside_a_drag_region_drags_the_window) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 800, 600);
  RnWin32View *header = tree.box(2, 0, 0, 800, 32, kTitleBarDragRegionId);
  root->insertChild(header, 0);
  header->insertChild(tree.box(3, 10, 5, 100, 20), 0);

  EXPECT(isTitleBarDragRegionAt(root, 20, 10));
  EXPECT(isTitleBarDragRegionAt(root, 400, 10));
}

TEST(a_no_drag_region_inside_a_drag_region_stays_pressable) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 800, 600);
  RnWin32View *header = tree.box(2, 0, 0, 800, 32, kTitleBarDragRegionId);
  root->insertChild(header, 0);
  RnWin32View *button = tree.box(3, 600, 0, 50, 32, kTitleBarNoDragRegionId);
  header->insertChild(button, 0);

  EXPECT(!isTitleBarDragRegionAt(root, 620, 10));

  // And a drag region nested inside that button drags again: the nearest
  // marker decides, not the outermost.
  button->insertChild(tree.box(4, 5, 5, 10, 10, kTitleBarDragRegionId), 0);
  EXPECT(isTitleBarDragRegionAt(root, 607, 7));
}

TEST(a_point_under_nothing_marked_is_ordinary_client_area) {
  Tree tree;
  RnWin32View *root = tree.box(1, 0, 0, 800, 600);
  root->insertChild(tree.box(2, 0, 0, 800, 32, kTitleBarDragRegionId), 0);
  root->insertChild(tree.box(3, 0, 32, 800, 568), 1);

  EXPECT(!isTitleBarDragRegionAt(root, 400, 300));
  // A miss altogether -- outside the root -- is not a drag either.
  EXPECT(!isTitleBarDragRegionAt(root, 900, 10));
}
