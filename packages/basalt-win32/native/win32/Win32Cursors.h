// A CSS cursor keyword as a Win32 cursor.
//
// `core/CursorNames.h` says what a React Native `cursor` value is called and
// each host answers for its own platform. GTK hands the keyword straight to
// `gtk_widget_set_cursor_from_name`, because GDK's names *are* CSS's; AppKit
// maps onto its own small set of NSCursors; and this is the Win32 map, onto the
// `IDC_` family that comes with the system.
//
// ## What the system has, and what it does not
//
// Thirteen `IDC_` cursors cover most of CSS's list, and several keywords land on
// the same one because Windows draws no difference: `move`, `all-scroll`, `grab`
// and `grabbing` are all `IDC_SIZEALL`, and the eight resize keywords collapse
// onto the four `IDC_SIZE*` arrows, which is exactly what the shell does for a
// window's own edges.
//
// Five keywords have nothing: `alias`, `cell`, `context-menu`, `copy`, `zoom-in`
// and `zoom-out` exist in the CSS list and there is no stock cursor for any of
// them. Windows ships the art for some of them inside shell32 and comctl32, as
// unnamed ordinals, which is the kind of thing that works until an update moves
// it -- so this answers "nothing" and the host leaves the pointer alone, the way
// AppKit's map leaves the holes in its own list alone. The dump still reports the
// keyword, so what an app asked for is visible either way.
//
// `none` is its own answer: Win32 hides the pointer with `SetCursor(nullptr)`,
// so it needs no cursor at all and is told apart from "nothing to do" by the
// `hidden` flag below.

#pragma once

#include <windows.h>

#include <string>

namespace basalt::win32 {

// What the host should do about a cursor keyword.
struct Win32Cursor {
  // The cursor to install, or null for "leave the pointer as it is" -- which is
  // both an unknown keyword and one Windows has nothing for.
  HCURSOR cursor = nullptr;
  // `cursor: 'none'`, which is a cursor of its own: no image, and the pointer
  // goes away rather than staying an arrow.
  bool hidden = false;
};

// The stock cursor for a keyword. Loaded rather than cached: `LoadCursorW` on a
// system cursor is a lookup in a shared table and does not need freeing, which
// is why this hands back a raw handle and nobody destroys it.
inline Win32Cursor win32CursorFor(const std::string &keyword) {
  Win32Cursor answer;
  if (keyword.empty()) {
    return answer;
  }
  if (keyword == "none") {
    answer.hidden = true;
    return answer;
  }

  const wchar_t *name = nullptr;
  if (keyword == "default") {
    name = IDC_ARROW;
  } else if (keyword == "pointer") {
    name = IDC_HAND;
  } else if (keyword == "text") {
    name = IDC_IBEAM;
  } else if (keyword == "vertical-text") {
    // No vertical I-beam in the stock set; the horizontal one is nearer than an
    // arrow.
    name = IDC_IBEAM;
  } else if (keyword == "crosshair") {
    name = IDC_CROSS;
  } else if (keyword == "wait") {
    name = IDC_WAIT;
  } else if (keyword == "progress") {
    // The arrow with the hourglass beside it, which is what `progress` means:
    // busy but still usable.
    name = IDC_APPSTARTING;
  } else if (keyword == "help") {
    name = IDC_HELP;
  } else if (keyword == "not-allowed" || keyword == "no-drop") {
    name = IDC_NO;
  } else if (keyword == "move" || keyword == "all-scroll" || keyword == "grab"
             || keyword == "grabbing") {
    name = IDC_SIZEALL;
  } else if (keyword == "ew-resize" || keyword == "col-resize" || keyword == "e-resize"
             || keyword == "w-resize") {
    name = IDC_SIZEWE;
  } else if (keyword == "ns-resize" || keyword == "row-resize" || keyword == "n-resize"
             || keyword == "s-resize") {
    name = IDC_SIZENS;
  } else if (keyword == "nesw-resize" || keyword == "ne-resize" || keyword == "sw-resize") {
    name = IDC_SIZENESW;
  } else if (keyword == "nwse-resize" || keyword == "nw-resize" || keyword == "se-resize") {
    name = IDC_SIZENWSE;
  }
  if (name != nullptr) {
    answer.cursor = LoadCursorW(nullptr, name);
  }
  return answer;
}

} // namespace basalt::win32
