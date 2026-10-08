// What a React Native `cursor` value is called, in CSS's own words.
//
// The prop is a desktop prop: a hand over a button, an I-beam over text, a
// resize arrow over a divider. Every host ignored it, and
// backlog/desktop-capabilities.md said the opposite -- its entry on cursor
// control described itself as the work left "beyond what the `cursor` style
// property covers", which nothing covered.
//
// ## Why a name rather than a per-host enum
//
// React Native's `Cursor` is CSS's keyword list, and so is GDK's: GTK takes the
// keyword straight through `gtk_widget_set_cursor_from_name`. AppKit has its own
// small set of NSCursors and maps the names onto it, losing the ones macOS has no
// cursor for. Keeping the keyword as the thing that crosses the seam means there
// is one place that says what `cursor: 'grab'` is called and each view layer
// answers for its own platform, rather than two enums that can drift apart in
// their middles.
//
// Header-only and in core because both mounting managers need it and both know
// React Native; the view layers do not, and they take a string.

#pragma once

#include <react/renderer/components/view/primitives.h>

namespace basalt {

// The CSS keyword, or nullptr for "nothing to apply" -- which is `auto`, meaning
// the cursor is whatever the surrounding view says it is, and `url`, which in
// CSS names a custom image that React Native carries nothing for.
inline const char *cursorName(facebook::react::Cursor cursor) {
  using facebook::react::Cursor;
  switch (cursor) {
    case Cursor::Auto:
      return nullptr;
    case Cursor::Alias:
      return "alias";
    case Cursor::AllScroll:
      return "all-scroll";
    case Cursor::Cell:
      return "cell";
    case Cursor::ColResize:
      return "col-resize";
    case Cursor::ContextMenu:
      return "context-menu";
    case Cursor::Copy:
      return "copy";
    case Cursor::Crosshair:
      return "crosshair";
    case Cursor::Default:
      return "default";
    case Cursor::EResize:
      return "e-resize";
    case Cursor::EWResize:
      return "ew-resize";
    case Cursor::Grab:
      return "grab";
    case Cursor::Grabbing:
      return "grabbing";
    case Cursor::Help:
      return "help";
    case Cursor::Move:
      return "move";
    case Cursor::NEResize:
      return "ne-resize";
    case Cursor::NESWResize:
      return "nesw-resize";
    case Cursor::NResize:
      return "n-resize";
    case Cursor::NSResize:
      return "ns-resize";
    case Cursor::NWResize:
      return "nw-resize";
    case Cursor::NWSEResize:
      return "nwse-resize";
    case Cursor::NoDrop:
      return "no-drop";
    case Cursor::None:
      return "none";
    case Cursor::NotAllowed:
      return "not-allowed";
    case Cursor::Pointer:
      return "pointer";
    case Cursor::Progress:
      return "progress";
    case Cursor::RowResize:
      return "row-resize";
    case Cursor::SResize:
      return "s-resize";
    case Cursor::SEResize:
      return "se-resize";
    case Cursor::SWResize:
      return "sw-resize";
    case Cursor::Text:
      return "text";
    case Cursor::Url:
      return nullptr;
    case Cursor::WResize:
      return "w-resize";
    case Cursor::Wait:
      return "wait";
    case Cursor::ZoomIn:
      return "zoom-in";
    case Cursor::ZoomOut:
      return "zoom-out";
  }
  return nullptr;
}

} // namespace basalt
