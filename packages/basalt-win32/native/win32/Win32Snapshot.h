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

} // namespace basalt::win32
