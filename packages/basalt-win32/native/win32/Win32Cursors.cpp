#include "Win32Cursors.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace basalt::win32 {
namespace {

// The six keywords Windows has no stock cursor for, drawn here.
//
// Each is a 32 by 32 image of straight sRGB pixels with an alpha channel, which
// `CreateIconIndirect` takes as the colour bitmap of a cursor. The art is this
// project's own and deliberately plain: a black shape with a white outline,
// which is what every Windows cursor is and what keeps it legible over both a
// light and a dark window.
//
// Drawn rather than borrowed. Windows keeps art for several of these inside
// shell32 and comctl32 as unnamed ordinals, which works until an update
// renumbers them; `alias` is the exception and is loaded from
// `%SystemRoot%\Cursors\aero_link.cur`, a real file with a real name, with the
// drawing below as its fallback.
constexpr int kSize = 32;

struct Art {
  std::array<std::uint32_t, static_cast<std::size_t>(kSize) * kSize> pixels{};

  void set(int x, int y, std::uint32_t colour) {
    if (x < 0 || y < 0 || x >= kSize || y >= kSize) {
      return;
    }
    pixels[static_cast<std::size_t>(y) * kSize + static_cast<std::size_t>(x)] = colour;
  }
};

// BGRA, which is the order a 32-bit DIB stores and `CreateDIBSection` hands
// back. Opaque black and opaque white are the only two colours any of these
// use; everything else is left transparent.
constexpr std::uint32_t kInk = 0xFF000000;
constexpr std::uint32_t kEdge = 0xFFFFFFFF;

// The arrow three of these are built on, as art rather than as geometry: a
// pointer is a shape everybody recognises and nobody can describe in numbers.
// `K` is the black fill, `W` the white outline, a space is transparent.
constexpr const char *const kArrow[] = {
    "K           ",
    "KK          ",
    "KWK         ",
    "KWWK        ",
    "KWWWK       ",
    "KWWWWK      ",
    "KWWWWWK     ",
    "KWWWWWWK    ",
    "KWWWWWWWK   ",
    "KWWWWWWWWK  ",
    "KWWWWWKKKKK ",
    "KWWKWWK     ",
    "KWK KWWK    ",
    "KK  KWWK    ",
    "K    KWWK   ",
    "     KWWK   ",
    "      KWK   ",
    "      KKK   ",
};

void drawArrow(Art &art) {
  int y = 0;
  for (const char *row : kArrow) {
    for (int x = 0; row[x] != '\0'; x++) {
      if (row[x] == 'K') {
        art.set(x, y, kInk);
      } else if (row[x] == 'W') {
        art.set(x, y, kEdge);
      }
    }
    y++;
  }
}

// A filled rectangle, which every badge below is built from.
void drawRect(Art &art, int left, int top, int width, int height, std::uint32_t colour) {
  for (int y = top; y < top + height; y++) {
    for (int x = left; x < left + width; x++) {
      art.set(x, y, colour);
    }
  }
}

// A plus or a minus, centred on (x, y), `reach` long each way and `thickness`
// thick. The horizontal bar alone is a minus.
void drawPlus(Art &art, int x, int y, int reach, int thickness, bool vertical,
              std::uint32_t colour) {
  drawRect(art, x - reach, y - thickness / 2, reach * 2 + 1, thickness, colour);
  if (vertical) {
    drawRect(art, x - thickness / 2, y - reach, thickness, reach * 2 + 1, colour);
  }
}

// A circle's outline, by the distance from its centre: a pixel is on the ring
// when it is within half a pixel of the radius, which is thin enough to read as
// a line and thick enough never to break.
void drawRing(Art &art, int cx, int cy, int radius, int thickness, std::uint32_t colour) {
  const int outer = radius + thickness;
  for (int y = cy - outer; y <= cy + outer; y++) {
    for (int x = cx - outer; x <= cx + outer; x++) {
      const int dx = x - cx;
      const int dy = y - cy;
      const int distance = dx * dx + dy * dy;
      if (distance <= outer * outer && distance >= radius * radius) {
        art.set(x, y, colour);
      }
    }
  }
}

// A diagonal line, for a magnifier's handle.
void drawDiagonal(Art &art, int fromX, int fromY, int length, int thickness,
                  std::uint32_t colour) {
  for (int step = 0; step < length; step++) {
    drawRect(art, fromX + step, fromY + step, thickness, thickness, colour);
  }
}

// The badge the arrow cursors carry at the bottom right: a white box with a
// black border, which is where every shell cursor of this family puts its mark.
void drawBadgeBox(Art &art) {
  drawRect(art, 12, 16, 14, 14, kInk);
  drawRect(art, 13, 17, 12, 12, kEdge);
}

Art copyArt() {
  Art art;
  drawArrow(art);
  drawBadgeBox(art);
  // A plus in the box, which is what `copy` means in every file manager.
  drawPlus(art, 19, 23, 4, 2, true, kInk);
  return art;
}

Art contextMenuArt() {
  Art art;
  drawArrow(art);
  drawBadgeBox(art);
  // Three lines, which is a menu at this size.
  drawRect(art, 15, 20, 8, 2, kInk);
  drawRect(art, 15, 24, 8, 2, kInk);
  drawRect(art, 15, 28, 8, 1, kInk);
  return art;
}

Art aliasArt() {
  Art art;
  drawArrow(art);
  drawBadgeBox(art);
  // An arrow turning up and to the right, which is the shortcut mark Windows
  // puts on a linked file.
  drawRect(art, 16, 26, 7, 2, kInk);
  drawRect(art, 21, 20, 2, 8, kInk);
  drawRect(art, 19, 20, 6, 2, kInk);
  drawRect(art, 20, 19, 2, 2, kInk);
  return art;
}

Art cellArt() {
  Art art;
  // A thick plus, centred, with a white outline so it reads over dark content.
  // `crosshair` is the thin one and is `IDC_CROSS`; this is the spreadsheet
  // cell cursor, which is heavier on purpose.
  drawPlus(art, 16, 16, 11, 7, true, kEdge);
  drawPlus(art, 16, 16, 10, 3, true, kInk);
  return art;
}

Art zoomArt(bool in) {
  Art art;
  // A magnifier: a ring, a handle running down to the right, and the sign
  // inside it.
  drawRing(art, 13, 13, 8, 3, kEdge);
  drawRing(art, 13, 13, 8, 2, kInk);
  drawDiagonal(art, 19, 19, 10, 4, kEdge);
  drawDiagonal(art, 19, 19, 10, 3, kInk);
  drawPlus(art, 13, 13, 4, 2, in, kInk);
  return art;
}

// The DIB and the mask `CreateIconIndirect` wants.
//
// The mask is required even for a 32-bit colour bitmap, which is the part worth
// knowing: it is left all zero, so every pixel is taken from the colour bitmap
// and its alpha decides what shows. A cursor built this way needs destroying,
// unlike a stock one, which is why they are cached below rather than made per
// call.
HCURSOR cursorFromArt(const Art &art, int hotX, int hotY) {
  BITMAPV5HEADER header{};
  header.bV5Size = sizeof(BITMAPV5HEADER);
  header.bV5Width = kSize;
  // Negative, so the rows run top down and the art above reads the way it is
  // written. A positive height is bottom up and would turn every cursor over.
  header.bV5Height = -kSize;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00FF0000;
  header.bV5GreenMask = 0x0000FF00;
  header.bV5BlueMask = 0x000000FF;
  header.bV5AlphaMask = 0xFF000000;

  HDC screen = GetDC(nullptr);
  void *bits = nullptr;
  HBITMAP colour = CreateDIBSection(screen,
                                    reinterpret_cast<BITMAPINFO *>(&header),
                                    DIB_RGB_COLORS,
                                    &bits,
                                    nullptr,
                                    0);
  ReleaseDC(nullptr, screen);
  if (colour == nullptr || bits == nullptr) {
    if (colour != nullptr) {
      DeleteObject(colour);
    }
    return nullptr;
  }
  std::memcpy(bits, art.pixels.data(), art.pixels.size() * sizeof(std::uint32_t));

  HBITMAP mask = CreateBitmap(kSize, kSize, 1, 1, nullptr);
  if (mask == nullptr) {
    DeleteObject(colour);
    return nullptr;
  }

  ICONINFO info{};
  info.fIcon = FALSE;
  info.xHotspot = static_cast<DWORD>(hotX);
  info.yHotspot = static_cast<DWORD>(hotY);
  info.hbmMask = mask;
  info.hbmColor = colour;
  HCURSOR cursor = CreateIconIndirect(&info);
  DeleteObject(mask);
  DeleteObject(colour);
  return cursor;
}


HCURSOR drawnCursorFor(const std::string &keyword) {
  if (keyword == "copy") {
    return cursorFromArt(copyArt(), 0, 0);
  }
  if (keyword == "context-menu") {
    return cursorFromArt(contextMenuArt(), 0, 0);
  }
  if (keyword == "alias") {
    // Drawn, like the other five, and not `%SystemRoot%\Cursors\aero_link.cur`.
    //
    // That file was preferred here on the reasoning that a real name beats an
    // unnamed ordinal inside shell32, which is sound and is still why nothing
    // else reaches into the shell. What it missed is what the file contains:
    // on Windows 11 aero_link.cur is "Link Select", the pointing hand -- the
    // same cursor `pointer` already gets from IDC_HAND. So `alias` and
    // `pointer` came back pixel for pixel identical, and CSS means two
    // different things by them.
    //
    // The art below is the shape `alias` is supposed to be: an arrow with the
    // shortcut badge turning up and to the right. It was already written and
    // never ran, because the file always loaded.
    return cursorFromArt(aliasArt(), 0, 0);
  }
  if (keyword == "cell") {
    // The hotspot is the middle of the plus rather than its corner, which is
    // what makes a cell cursor point at the cell it is over.
    return cursorFromArt(cellArt(), 16, 16);
  }
  if (keyword == "zoom-in") {
    return cursorFromArt(zoomArt(true), 13, 13);
  }
  if (keyword == "zoom-out") {
    return cursorFromArt(zoomArt(false), 13, 13);
  }
  return nullptr;
}

} // namespace

HCURSOR win32DrawnCursorFor(const std::string &keyword) {
  // Made once and kept for the life of the process, which is the same contract
  // the stock cursors have: a caller installs the handle and never frees it.
  // Destroying them at exit would buy nothing -- the window system drops a
  // process's cursors with the process.
  static std::mutex lock;
  static std::unordered_map<std::string, HCURSOR> made;

  const std::lock_guard<std::mutex> held(lock);
  const auto found = made.find(keyword);
  if (found != made.end()) {
    return found->second;
  }
  HCURSOR cursor = drawnCursorFor(keyword);
  made.emplace(keyword, cursor);
  return cursor;
}

} // namespace basalt::win32
