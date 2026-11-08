// Rendering a view tree without showing it -- to a PNG, or to pixels a test can
// read.
//
// The PNG is shared by the demo and, later, the mount harness, and it is the
// only way either of them proves anything: a tree dump prints the frames that
// were *set*, which looks identical whether or not the transform composition,
// the clip and the paint order are right. A picture does not.
//
// The pixels are the same render with the encoder left off, and they exist
// because of a gap `docs/BACKLOG.md` has carried since GTK: "the widget tree
// says a view has a colour and a frame, not that the right pixels reached the
// screen -- GTK's cairo renderer mangled every transform in the demo and no
// test noticed". Neither of the other hosts can close that cheaply. GTK needs a
// display server and macOS needs an offscreen NSWindow and a display cycle;
// Direct2D renders into a WIC bitmap with no window, no device and no display,
// so on this platform a rendering assertion costs a function call. That is also
// what lets the Windows suite run on a headless CI machine with none of the
// xvfb arrangement `docs/TESTING.md` describes for GTK.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace basalt::win32 {

class RnWin32View;

// Straight (not premultiplied) sRGB, 0..255, as the components went in.
struct RnPixel {
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  uint8_t alpha = 0;
};

class RnPixels {
 public:
  RnPixels(unsigned width, unsigned height, std::vector<uint8_t> bgra)
      : width_(width), height_(height), bgra_(std::move(bgra)) {}

  unsigned width() const { return width_; }
  unsigned height() const { return height_; }
  bool empty() const { return bgra_.empty(); }

  // The colour at a point, unpremultiplied. Out of bounds reads as transparent
  // black rather than throwing: a test that asks about the wrong pixel should
  // fail on the colour, where the message says what it expected.
  RnPixel at(unsigned x, unsigned y) const;

 private:
  unsigned width_ = 0;
  unsigned height_ = 0;
  std::vector<uint8_t> bgra_; // premultiplied BGRA, stride = width * 4
};

// Renders `root` and its subtree at the root's own size, onto transparent
// black. An empty result means the render failed.
RnPixels renderToPixels(const RnWin32View &root);

// The same render, encoded as a PNG at `path`. Returns false if the tree could
// not be rendered or the file could not be written.
bool writeSnapshot(const RnWin32View &root, const std::string &path);

// A photograph of the live window instead of a render of the tree, through
// `PrintWindow` with PW_RENDERFULLCONTENT, which asks DWM for what it
// composited.
//
// Everything above renders `RnWin32View`s and is the right answer for a test:
// no window has to exist, nothing has to be visible, and the result is the
// size the frames say. Two things are not in that tree, and backlog entry 3
// in docs/backlog/platform-windows.md has named both since 2026-10-13. A
// `<TextInput>`'s peer is a child window, so a field renders as its
// background with no text, no placeholder and no caret. A `<Canvas>` shows
// the last frame delivered to the live window, which an offscreen pass never
// had. The caption buttons are a third: DWM draws them over the app's header
// and no view knows about them.
//
// So this is for a picture of the application as a person sees it, and the
// render above stays what the tests read. It needs a real window, and the
// pixels are the window's own, which means the machine's DPI rather than
// BASALT_SNAPSHOT_SCALE.
//
// `window` is an HWND, as `void *` so this header stays free of windows.h.
// Returns false if there is no window, if PrintWindow refused, or if the
// file could not be written.
bool captureWindow(void *window, const std::string &path);

} // namespace basalt::win32
