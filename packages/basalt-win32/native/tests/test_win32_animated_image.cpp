// An animated GIF: the frames WIC decodes, composited, and paced by the clamp
// all three hosts share.
//
// Against real pixels rather than against which frame the view thinks it is on,
// for the reason tests/test_win32_paint.cpp exists: a view that advanced its
// index correctly and kept painting the first bitmap would pass either way.
//
// The file is a real GIF, four pixels square and ninety-five bytes, inline as
// base64 so this needs no asset on disk. It is the same fixture the GTK suite
// uses, which is what makes the two suites comparable. Written with ImageMagick:
//
//   magick -loop 0 -delay 8 -size 4x4 xc:'#ff0000' \
//                 -delay 4 -size 4x4 xc:'#0000ff' anim.gif
//
// so frame one is red for 80ms and frame two is blue for 40ms, forever. The
// delays differ on purpose: equal ones hide an engine that uses the first
// frame's delay for all of them.
//
// Where this host differs from GTK is the ownership. gdk-pixbuf owns the frames
// and the clock behind `GdkPixbufAnimationIter`, so that host asks it for a
// frame; WIC hands over indexed frames and raw metadata and nothing else, so
// the compositing, the delays and the loop count are all this host's, and each
// of them is checked here.

#include "TestHarness.h"

#include "ImageBytes.h"
#include "RnWin32Image.h"
#include "RnWin32View.h"
#include "Win32ImageLoader.h"
#include "Win32Snapshot.h"

#include <cstdint>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using basalt::win32::RnImageFit;
using basalt::win32::RnPixel;
using basalt::win32::RnPixels;
using basalt::win32::RnWin32Image;
using basalt::win32::RnWin32ImageFrames;
using basalt::win32::RnWin32View;
using basalt::win32::Win32ImageLoader;

namespace {

const char *const kAnimatedGifBase64 =
    "R0lGODlhBAAEAPAAAP8AAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACAAAACwAAAAABAAEAAAC"
    "BISPCQUAIfkEAAQAAAAsAAAAAAQABACAAAD/AAAAAgSEjwkFADs=";

// The same picture with no delays at all, which is what a great many GIFs carry
// and what the shared clamp exists for:
//
//   magick -loop 0 -size 4x4 xc:'#ff0000' -size 4x4 xc:'#0000ff' anim.gif
const char *const kInstantGifBase64 =
    "R0lGODlhBAAEAPAAAP8AAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQAAAAAACwAAAAABAAEAAAC"
    "BISPCQUAIfkEAAAAAAAsAAAAAAQABACAAAD/AAAAAgSEjwkFADs=";

// A GIF with no NETSCAPE2.0 extension at all, which is what "play once" looks
// like in the format: `magick -loop 1 ...` writes no application extension
// rather than a count of one.
const char *const kOnceGifBase64 =
    "R0lGODlhBAAEAPAAAP8AAAAAACH5BAAIAAAALAAAAAAEAAQAAAIEhI8JBQAh+QQABAAAACwAAAAA"
    "BAAEAIAAAP8AAAACBISPCQUAOw==";

// The still PNG the loader tests use: one frame, no delays.
const char *const kStillPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAIAAAB7QOjdAAAADUlEQVR42mP4zwAE/wEHAAH/PX2MSQAAAABJRU5ErkJggg==";

std::string dataUri(const char *mediaType, const char *base64) {
  return std::string("data:") + mediaType + ";base64," + base64;
}

// The bytes inside one of those strings, through the same `data:` reader the
// loader fetches with, so the test needs no base64 decoder of its own.
std::string decode(const char *mediaType, const char *base64) {
  std::string bytes;
  std::string error;
  basalt::fetchImageBytes(dataUri(mediaType, base64), &bytes, &error);
  return bytes;
}

RnWin32ImageFrames framesOf(const char *base64) {
  const std::string bytes = decode("image/gif", base64);
  if (bytes.empty()) {
    return {};
  }
  return RnWin32Image::framesFromEncodedBytes(
      reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
}

// A view holding an animation, rendered so the colour of a frame is readable.
// The box is twenty square and the image four, so the default fit fills it and
// the middle pixel is the image's colour.
class View {
 public:
  explicit View(const RnWin32ImageFrames &frames) : view_(std::make_unique<RnWin32View>(1)) {
    view_->setFrame(0.0f, 0.0f, 20.0f, 20.0f);
    // The order the mounting manager uses: the still picture, then the frames.
    view_->setImage(frames.frames.empty() ? nullptr : frames.frames.front(), RnImageFit::Cover);
    view_->setImageFrames(frames.frames, frames.delaysMs, frames.loopCount);
  }

  RnWin32View *get() const { return view_.get(); }

  RnPixel centre() const {
    const RnPixels pixels = basalt::win32::renderToPixels(*view_);
    return pixels.at(10, 10);
  }

 private:
  std::unique_ptr<RnWin32View> view_;
};

} // namespace

// What WIC hands over, which is the half gdk-pixbuf does for the other host:
// two frames, the file's own delays, and "forever".
TEST(win32_animated_image_decodes_every_frame) {
  const RnWin32ImageFrames frames = framesOf(kAnimatedGifBase64);
  EXPECT(frames.animated());
  if (!frames.animated()) {
    return;
  }
  EXPECT_EQ(frames.frames.size(), 2u);
  EXPECT_EQ(frames.delaysMs.size(), 2u);
  // Hundredths of a second in the file, milliseconds here.
  EXPECT_EQ(frames.delaysMs[0], 80u);
  EXPECT_EQ(frames.delaysMs[1], 40u);
  // NETSCAPE2.0 with a count of zero, which is what a looping GIF writes.
  EXPECT_EQ(frames.loopCount, 0u);
  // Four pixels square: the logical screen, which is what every frame is
  // composited onto.
  EXPECT_EQ(frames.frames[0]->width(), 4u);
  EXPECT_EQ(frames.frames[0]->height(), 4u);
}

TEST(win32_animated_image_the_frame_follows_the_time_it_is_advanced_by) {
  const RnWin32ImageFrames frames = framesOf(kAnimatedGifBase64);
  EXPECT(frames.animated());
  if (!frames.animated()) {
    return;
  }

  View view(frames);

  // Frame one, red, which is also what a still <Image> of this file paints.
  EXPECT(view.centre().red > 200);
  EXPECT(view.centre().blue < 60);

  // Still frame one at 79ms: the delay is the file's and not a guess.
  view.get()->advanceImageAnimation(79.0);
  EXPECT(view.centre().red > 200);

  // Frame two, blue, one millisecond later.
  view.get()->advanceImageAnimation(1.0);
  EXPECT(view.centre().blue > 200);
  EXPECT(view.centre().red < 60);

  // And round again after the second frame's own, shorter delay: 40ms, not
  // another 80.
  view.get()->advanceImageAnimation(40.0);
  EXPECT(view.centre().red > 200);
}

TEST(win32_animated_image_says_how_long_until_the_next_frame) {
  const RnWin32ImageFrames frames = framesOf(kAnimatedGifBase64);
  EXPECT(frames.animated());
  if (!frames.animated()) {
    return;
  }

  View view(frames);
  // What the host's timer stops on: the whole 80ms at the start of frame one,
  // and what is left of it in the middle.
  EXPECT_NEAR(view.get()->advanceImageAnimation(0.0), 80.0, 0.01);
  EXPECT_NEAR(view.get()->advanceImageAnimation(30.0), 50.0, 0.01);
  // Into frame two, which is 40ms long.
  EXPECT_NEAR(view.get()->advanceImageAnimation(50.0), 40.0, 0.01);
}

// A GIF that asks for no delay at all is paced at a tenth of a second.
//
// **The clamp is load-bearing on this host, and the first assertion is what
// says so rather than a claim about WIC.** `/grctlext/Delay` is the file's own
// number, so if WIC hands over the zero this reads zero and the hundred below
// is core/ImageAnimation.h's doing; if some future WIC substituted a default,
// this line would fail and say so. gdk-pixbuf does substitute, which is why the
// equivalent GTK test records that it does not discriminate the clamp there.
TEST(win32_animated_image_a_delay_of_zero_is_paced_like_a_browsers) {
  const RnWin32ImageFrames frames = framesOf(kInstantGifBase64);
  EXPECT(frames.animated());
  if (!frames.animated()) {
    return;
  }
  EXPECT_EQ(frames.delaysMs[0], 0u);

  View view(frames);
  EXPECT_NEAR(view.get()->advanceImageAnimation(0.0), 100.0, 0.01);

  // Nothing at 99ms, and the next frame at 100.
  view.get()->advanceImageAnimation(99.0);
  EXPECT(view.centre().red > 200);
  view.get()->advanceImageAnimation(1.0);
  EXPECT(view.centre().blue > 200);
}

TEST(win32_animated_image_is_reported_in_the_tree) {
  const RnWin32ImageFrames frames = framesOf(kAnimatedGifBase64);
  EXPECT(frames.animated());
  if (!frames.animated()) {
    return;
  }

  View view(frames);
  EXPECT(view.get()->describeTree().find("animated=1") != std::string::npos);
  EXPECT(view.get()->hasAnimatedImage());

  // A parent reports a child's animation, which is what the host's timer asks
  // of the root: an <Image> is never the root of a surface.
  auto parent = std::make_unique<RnWin32View>(2);
  parent->setFrame(0.0f, 0.0f, 20.0f, 20.0f);
  parent->insertChild(view.get(), 0);
  EXPECT(parent->hasAnimatedImage());
  parent->removeChild(view.get());
  EXPECT(!parent->hasAnimatedImage());
  parent->insertChild(view.get(), 0);

  // Cleared with the frames, so an <Image> whose source becomes a still one
  // does not keep claiming to move and does not keep the timer awake.
  view.get()->setImageFrames({}, {}, 1u);
  EXPECT(!view.get()->hasAnimatedImage());
  EXPECT(!parent->hasAnimatedImage());
  EXPECT(view.get()->describeTree().find("animated=1") == std::string::npos);
  // The picture stays: clearing the animation is not clearing the image.
  EXPECT(view.get()->describeTree().find("texture=4x4") != std::string::npos);
}

// A re-mount must not restart the animation, which is the bug this shape of
// mounting manager invites: every mutation that touches an <Image> re-applies
// its props, a layout-only one included, so a reaction GIF would stutter back
// to the top on every resize.
TEST(win32_animated_image_survives_the_props_being_reapplied) {
  const RnWin32ImageFrames frames = framesOf(kAnimatedGifBase64);
  EXPECT(frames.animated());
  if (!frames.animated()) {
    return;
  }

  View view(frames);
  view.get()->advanceImageAnimation(100.0);
  EXPECT(view.centre().blue > 200);

  // What the mounting manager does on a mutation: the same frames again, from
  // the loader's cache, and the still picture that is now the current frame.
  view.get()->setImage(view.get()->image(), RnImageFit::Cover);
  view.get()->setImageFrames(frames.frames, frames.delaysMs, frames.loopCount);
  EXPECT(view.centre().blue > 200);

  // A different animation does start over, which is the same check from the
  // other side: the frames are what tell them apart.
  const RnWin32ImageFrames other = framesOf(kAnimatedGifBase64);
  view.get()->setImageFrames(other.frames, other.delaysMs, other.loopCount);
  EXPECT(view.centre().red > 200);
}

TEST(win32_animated_image_a_gif_that_does_not_loop_plays_once) {
  const RnWin32ImageFrames frames = framesOf(kOnceGifBase64);
  EXPECT(frames.animated());
  if (!frames.animated()) {
    return;
  }
  // No application extension means one pass, which is also what ImageIO
  // answers for these bytes on the AppKit host.
  EXPECT_EQ(frames.loopCount, 1u);

  View view(frames);
  // Halfway through the first frame's 80ms, still running.
  EXPECT(view.get()->advanceImageAnimation(60.0) > 0.0);
  // One pass is 120ms, after which nothing more is due: the host's timer stops
  // on exactly this, which is what keeps a played-out GIF from waking the
  // process for the rest of the session.
  EXPECT_NEAR(view.get()->advanceImageAnimation(60.0), 0.0, 0.01);
  // And the last frame stays, rather than snapping back to the first.
  EXPECT(view.centre().blue > 200);
}

TEST(win32_animated_image_a_still_file_is_not_animated) {
  const std::string bytes = decode("image/png", kStillPngBase64);
  EXPECT(!bytes.empty());
  const RnWin32ImageFrames frames = RnWin32Image::framesFromEncodedBytes(
      reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size());
  // Empty rather than one frame: a still image has no animation, and the view
  // tells them apart by the count.
  EXPECT(!frames.animated());
  EXPECT(frames.frames.empty());

  // The same bytes still decode as a picture, which is the thing a still
  // <Image> paints: the two decodes are independent.
  EXPECT(RnWin32Image::fromEncodedBytes(
             reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size()) != nullptr);

  View view(frames);
  EXPECT(!view.get()->hasAnimatedImage());
  EXPECT_NEAR(view.get()->advanceImageAnimation(500.0), 0.0, 0.01);
}

// And the loader's half: that an animated file is recognised as one, that the
// frames are the same objects on a second load -- which is what lets a view
// tell a re-mount from a new animation -- and that a still file has none.
TEST(win32_animated_image_the_loader_keeps_the_frames) {
  Win32ImageLoader loader;
  const std::string uri = dataUri("image/gif", kAnimatedGifBase64);

  // No UI thread is installed here, so the load is synchronous; see the top of
  // tests/test_win32_imageloader.cpp.
  bool done = false;
  loader.load(uri, [&](std::shared_ptr<RnWin32Image> image, const std::string &error) {
    EXPECT(image != nullptr);
    EXPECT(error.empty());
    done = true;
  });
  EXPECT(done);

  const RnWin32ImageFrames frames = loader.animation(uri);
  EXPECT(frames.animated());
  EXPECT_EQ(frames.frames.size(), 2u);

  bool again = false;
  loader.load(uri, [&](std::shared_ptr<RnWin32Image>, const std::string &) { again = true; });
  EXPECT(again);
  EXPECT(loader.animation(uri).frames == frames.frames);

  // A still image has none, rather than a one-frame animation nobody can use.
  const std::string png = dataUri("image/png", kStillPngBase64);
  bool still = false;
  loader.load(png, [&](std::shared_ptr<RnWin32Image> image, const std::string &) {
    EXPECT(image != nullptr);
    still = true;
  });
  EXPECT(still);
  EXPECT(!loader.animation(png).animated());
  EXPECT(loader.animation(png).frames.empty());

  // And a URI nobody asked for: an empty result rather than a lookup that
  // inserts one, which is what the mounting manager leans on when it applies
  // the animation of an <Image> that has none.
  EXPECT(loader.animation("data:image/gif;base64,nonsense").frames.empty());
}
