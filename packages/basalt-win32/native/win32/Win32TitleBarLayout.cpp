#include "Win32TitleBarLayout.h"

#include "RnWin32View.h"

#include <cmath>
#include <string>

namespace basalt::win32 {

bool isTitleBarDragRegionAt(RnWin32View *root, float x, float y) {
  for (RnWin32View *view = hitTest(root, x, y); view != nullptr; view = view->parent()) {
    const std::string &id = view->nativeId();
    if (id == kTitleBarDragRegionId) {
      return true;
    }
    if (id == kTitleBarNoDragRegionId) {
      return false;
    }
  }
  return false;
}

CaptionMetrics captionMetricsForDpi(uint32_t dpi) {
  const float scale = dpi == 0 ? 1.0f : static_cast<float>(dpi) / 96.0f;
  return CaptionMetrics{std::round(kCaptionHeightDips * scale),
                        std::round(kCaptionButtonWidthDips * scale)};
}

CaptionButton captionButtonAt(float x, float y, float clientWidth, const CaptionMetrics &metrics) {
  if (y < 0 || y >= metrics.height || x < 0 || x >= clientWidth || metrics.buttonWidth <= 0) {
    return CaptionButton::None;
  }
  const float fromRight = clientWidth - x;
  if (fromRight <= metrics.buttonWidth) {
    return CaptionButton::Close;
  }
  if (fromRight <= 2 * metrics.buttonWidth) {
    return CaptionButton::Maximize;
  }
  if (fromRight <= 3 * metrics.buttonWidth) {
    return CaptionButton::Minimize;
  }
  return CaptionButton::None;
}

} // namespace basalt::win32
