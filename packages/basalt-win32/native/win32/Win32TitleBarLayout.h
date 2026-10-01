// Where a hidden title bar's pieces are: which caption button a point is over,
// how big the caption is at a DPI, and whether a point drags the window.
//
// The geometry half of Win32TitleBar, kept apart from it so that it needs no
// window, no DWM and no React Native -- only a view tree and a size -- and can
// be tested with none of them. A custom caption fails in ways a person notices
// at once and a test does not, unless the test asks exactly these questions.

#pragma once

#include "TitleBarRegions.h"

#include <cstdint>

namespace basalt::win32 {

class RnWin32View;

// What <TitleBar.DragRegion> and <TitleBar.NoDragRegion> set as a view's
// nativeID, under this namespace's own names so that everything already
// written against them keeps compiling.
//
// The values come from core/TitleBarRegions.h rather than being spelled again
// here: GTK reads the same two strings, and src/TitleBar.tsx writes them, so a
// literal in this file would be the third copy of something that only works
// when all of them agree.
inline constexpr const char *kTitleBarDragRegionId = basalt::kTitleBarDragRegionId;
inline constexpr const char *kTitleBarNoDragRegionId = basalt::kTitleBarNoDragRegionId;

// Whether a point, in the root's coordinates, drags the window.
//
// The deepest view under the point and then each of its ancestors are asked in
// turn, and the nearest one marked either way decides. So a button inside a
// drag region stays a button once it is marked no-drag, a label inside a drag
// region drags without being marked at all, and a point under nothing marked
// is ordinary client area.
bool isTitleBarDragRegionAt(RnWin32View *root, float x, float y);

enum class CaptionButton { None, Minimize, Maximize, Close };

// Windows 11's own caption sizes, in device-independent pixels.
inline constexpr float kCaptionHeightDips = 32.0f;
inline constexpr float kCaptionButtonWidthDips = 46.0f;

// Those sizes at a DPI, in client pixels -- which on this host are also the
// surface root's units, because the root is sized to the client rectangle.
struct CaptionMetrics {
  float height{0};
  float buttonWidth{0};
};
CaptionMetrics captionMetricsForDpi(uint32_t dpi);

// The caption button under a point in client coordinates, for a window
// `clientWidth` wide. The three sit flush right, in the order Windows puts
// them: minimise, maximise, close.
CaptionButton captionButtonAt(float x, float y, float clientWidth, const CaptionMetrics &metrics);

} // namespace basalt::win32
