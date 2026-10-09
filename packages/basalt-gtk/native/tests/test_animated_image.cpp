// Animated images: the frames gdk-pixbuf owns, paced by the clamp both hosts
// share.
//
// Against real pixels rather than against which frame the widget thinks it is
// on, for the reason tests/test_gtk_paint.cpp exists: a view that advanced its
// iterator correctly and kept painting the first texture would pass either way.
//
// The file is a real GIF, four pixels square and ninety-five bytes, inline as
// base64 so this needs no asset on disk. Written with ImageMagick:
//
//   magick -loop 0 -delay 8 -size 4x4 xc:'#ff0000' \
//                 -delay 4 -size 4x4 xc:'#0000ff' anim.gif
//
// so frame one is red for 80ms and frame two is blue for 40ms, forever. The
// delays differ on purpose: equal ones hide an engine that uses the first
// frame's delay for all of them.

#include "TestHarness.h"

#include "GtkImageLoader.h"
#include "GtkPixels.h"
#include "RnView.h"

#include <sstream>
#include <string>

using basalt::testing::RnPixel;
using basalt::testing::RnPixels;
using basalt::testing::renderView;

// gdk-pixbuf 2.44 deprecated its animation API and gdk-pixbuf has nothing to
// replace it with; see GtkImageLoader.cpp for why this host uses it anyway.
// The tests have to say the same thing the code does.
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

namespace {

const char *const kAnimatedGifBase64 =
    "R0lGODlhBAAEAPAAAP8AAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQACAAAACwAAAAABAAEAAAC"
    "BISPCQUAIfkEAAQAAAAsAAAAAAQABACAAAD/AAAAAgSEjwkFADs=";

// The same picture with no delays at all, which is what a great many GIFs
// carry and what the shared clamp exists for:
//
//   magick -loop 0 -size 4x4 xc:'#ff0000' -size 4x4 xc:'#0000ff' anim.gif
const char *const kInstantGifBase64 =
    "R0lGODlhBAAEAPAAAP8AAAAAACH/C05FVFNDQVBFMi4wAwEAAAAh+QQAAAAAACwAAAAABAAEAAAC"
    "BISPCQUAIfkEAAAAAAAsAAAAAAQABACAAAD/AAAAAgSEjwkFADs=";

std::string gifUri(const char *base64) {
  return std::string("data:image/gif;base64,") + base64;
}

// The animation inside one of those strings, decoded the way the loader
// decodes it.
GdkPixbufAnimation *animationFrom(const char *base64) {
  gsize length = 0;
  guchar *bytes = g_base64_decode(base64, &length);
  GBytes *wrapped = g_bytes_new_take(bytes, length);
  GInputStream *stream = g_memory_input_stream_new_from_bytes(wrapped);
  GError *error = nullptr;
  GdkPixbufAnimation *animation = gdk_pixbuf_animation_new_from_stream(stream, nullptr, &error);
  g_clear_error(&error);
  g_object_unref(stream);
  g_bytes_unref(wrapped);
  return animation;
}

// Runs the main loop until `done`, bounded so a hang fails rather than blocks.
bool pumpUntil(const bool &done) {
  for (int i = 0; i < 20000 && !done; ++i) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(500);
  }
  return done;
}

class View {
 public:
  explicit View(GdkPixbufAnimation *animation) : view_(rn_view_new(1)) {
    g_object_ref_sink(view_);
    rn_view_set_frame(view_, 0.0F, 0.0F, 20.0F, 20.0F);
    rn_view_set_animation(view_, animation);
  }
  ~View() { g_object_unref(view_); }

  View(const View &) = delete;
  View &operator=(const View &) = delete;

  RnView *get() const { return view_; }

  // The colour at the middle of the view, which is where the image is: the fit
  // defaults to the one that fills the box.
  RnPixel centre() const {
    const RnPixels pixels = renderView(view_, 20, 20);
    return pixels.at(10, 10);
  }

 private:
  RnView *view_;
};

} // namespace

TEST(animated_image_the_frame_follows_the_time_it_is_advanced_by) {
  GdkPixbufAnimation *animation = animationFrom(kAnimatedGifBase64);
  EXPECT(animation != nullptr);
  if (animation == nullptr) {
    return;
  }
  EXPECT(gdk_pixbuf_animation_is_static_image(animation) == FALSE);

  View view(animation);
  EXPECT(rn_view_is_animated(view.get()) == TRUE);

  // Frame one, red, which is also what a still <Image> of this file paints.
  EXPECT(view.centre().red > 200);
  EXPECT(view.centre().blue < 60);

  // Still frame one at 79ms: the delay is the file's and not a guess.
  rn_view_advance_animation(view.get(), 79.0);
  EXPECT(view.centre().red > 200);

  // Frame two, blue, one millisecond later.
  rn_view_advance_animation(view.get(), 1.0);
  EXPECT(view.centre().blue > 200);
  EXPECT(view.centre().red < 60);

  // And round again after the second frame's own, shorter delay: 40ms, not
  // another 80.
  rn_view_advance_animation(view.get(), 40.0);
  EXPECT(view.centre().red > 200);

  g_object_unref(animation);
}

TEST(animated_image_says_how_long_until_the_next_frame) {
  GdkPixbufAnimation *animation = animationFrom(kAnimatedGifBase64);
  EXPECT(animation != nullptr);
  if (animation == nullptr) {
    return;
  }

  View view(animation);
  // What the tick callback stops on, which is why this is answered rather than
  // kept private: the whole 80ms at the start of frame one, and what is left of
  // it in the middle.
  EXPECT_NEAR(rn_view_advance_animation(view.get(), 0.0), 80.0, 0.01);
  EXPECT_NEAR(rn_view_advance_animation(view.get(), 30.0), 50.0, 0.01);
  // Into frame two, which is 40ms long.
  EXPECT_NEAR(rn_view_advance_animation(view.get(), 50.0), 40.0, 0.01);

  g_object_unref(animation);
}

// A GIF that asks for no delay at all, which is the common case rather than a
// corner, is paced at a tenth of a second.
//
// **This one does not discriminate the shared clamp on this host, and the
// measurement is why it is still here.** gdk-pixbuf's own GIF loader already
// answers 100 for a file that asks for 0, measured by printing the raw
// `gdk_pixbuf_animation_iter_get_delay_time` for this fixture, so
// core/ImageAnimation.h's clamp changes nothing here and this test passes
// without it. What it pins is the behaviour, which is what an app sees. The
// clamp is load-bearing on the AppKit host, where ImageIO hands over the
// file's zero unchanged: see `appkit_the_loader_leaves_a_zero_delay_alone`
// and the core tests.
TEST(animated_image_a_delay_of_zero_is_paced_like_a_browsers) {
  GdkPixbufAnimation *animation = animationFrom(kInstantGifBase64);
  EXPECT(animation != nullptr);
  if (animation == nullptr) {
    return;
  }

  View view(animation);
  EXPECT_NEAR(rn_view_advance_animation(view.get(), 0.0), 100.0, 0.01);

  // Nothing at 99ms, and the next frame at 100: without the clamp gdk-pixbuf's
  // own answer for this file decides, and it is not a hundred.
  rn_view_advance_animation(view.get(), 99.0);
  EXPECT(view.centre().red > 200);
  rn_view_advance_animation(view.get(), 1.0);
  EXPECT(view.centre().blue > 200);

  g_object_unref(animation);
}

TEST(animated_image_is_reported_in_the_tree) {
  GdkPixbufAnimation *animation = animationFrom(kAnimatedGifBase64);
  EXPECT(animation != nullptr);
  if (animation == nullptr) {
    return;
  }

  View view(animation);
  char *dump = rn_view_describe_tree(view.get());
  EXPECT(std::string(dump).find("animated=1") != std::string::npos);
  g_free(dump);

  // Cleared with the animation, so an <Image> whose source becomes a still one
  // does not keep claiming to move.
  rn_view_set_animation(view.get(), nullptr);
  EXPECT(rn_view_is_animated(view.get()) == FALSE);
  dump = rn_view_describe_tree(view.get());
  EXPECT(std::string(dump).find("animated=1") == std::string::npos);
  g_free(dump);

  g_object_unref(animation);
}

// A re-mount must not restart the animation, which is the bug this shape of
// mounting manager invites: every mutation that touches an <Image> re-applies
// the texture, a layout-only one included, so a spinner would stutter back to
// the top on every resize.
TEST(animated_image_survives_the_props_being_reapplied) {
  GdkPixbufAnimation *animation = animationFrom(kAnimatedGifBase64);
  EXPECT(animation != nullptr);
  if (animation == nullptr) {
    return;
  }

  View view(animation);
  rn_view_advance_animation(view.get(), 100.0);
  EXPECT(view.centre().blue > 200);

  // What the mounting manager does on a mutation: the same animation again.
  rn_view_set_animation(view.get(), animation);
  EXPECT(view.centre().blue > 200);

  // A different animation does start over, which is the same check from the
  // other side: the object is what tells them apart.
  GdkPixbufAnimation *other = animationFrom(kAnimatedGifBase64);
  rn_view_set_animation(view.get(), other);
  EXPECT(view.centre().red > 200);
  g_object_unref(other);

  g_object_unref(animation);
}

TEST(animated_image_a_still_file_is_not_animated) {
  // The PNG the loader tests use, which has one frame and no delays.
  const char *still =
      "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAIAAAB7QOjdAAAADUlEQVR42mP4zwAE/wEHAAH/PX2MSQAAAABJRU5ErkJggg==";
  GdkPixbufAnimation *animation = animationFrom(still);
  EXPECT(animation != nullptr);
  if (animation == nullptr) {
    return;
  }
  // gdk-pixbuf answers for any still image with a static animation, so this is
  // the thing the loader and the view both check.
  EXPECT(gdk_pixbuf_animation_is_static_image(animation) == TRUE);

  View view(animation);
  EXPECT(rn_view_is_animated(view.get()) == FALSE);
  EXPECT_NEAR(rn_view_advance_animation(view.get(), 500.0), 0.0, 0.01);

  g_object_unref(animation);
}

// And the loader's half: that an animated file is recognised as one, and a
// still file is not.
TEST(animated_image_the_loader_keeps_the_animation) {
  basalt::GtkImageLoader loader;
  const std::string uri = gifUri(kAnimatedGifBase64);

  bool done = false;
  loader.load(uri, [&](GdkTexture *texture, const std::string &) {
    EXPECT(texture != nullptr);
    done = true;
  });
  EXPECT(pumpUntil(done));

  GdkPixbufAnimation *animation = loader.animation(uri);
  EXPECT(animation != nullptr);
  EXPECT(animation != nullptr && gdk_pixbuf_animation_is_static_image(animation) == FALSE);

  // The same object on a second load, which is what lets a view tell a re-mount
  // from a new animation.
  bool again = false;
  loader.load(uri, [&](GdkTexture *, const std::string &) { again = true; });
  EXPECT(pumpUntil(again));
  EXPECT(loader.animation(uri) == animation);

  // A still image has none, rather than a static one nobody can use.
  const std::string png =
      "data:image/png;base64,"
      "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAIAAAB7QOjdAAAADUlEQVR42mP4zwAE/wEHAAH/PX2MSQAAAABJRU5ErkJggg==";
  bool still = false;
  loader.load(png, [&](GdkTexture *, const std::string &) { still = true; });
  EXPECT(pumpUntil(still));
  EXPECT(loader.animation(png) == nullptr);
}

// A GIF with no NETSCAPE2.0 extension at all, which is what "play once" looks
// like in the format:
//
//   magick -loop 1 -delay 8 -size 4x4 xc:'#ff0000' \
//                 -delay 4 -size 4x4 xc:'#0000ff' once.gif
//
// writes no application extension rather than a count of one.
//
// **Both hosts stop after one pass, which was measured rather than assumed.**
// gdk-pixbuf's iterator reports a delay of -1 at the end of this file, which
// `rn_view_animation_delay_ms` turns into "nothing more is due"; ImageIO on the
// other host answers a loop count of 1 for the same bytes. So neither host had
// to be taught about an absent extension.
TEST(animated_image_a_gif_that_does_not_loop_plays_once) {
  const char *once =
      "R0lGODlhBAAEAPAAAP8AAAAAACH5BAAIAAAALAAAAAAEAAQAAAIEhI8JBQAh+QQABAAAACwAAAAA"
      "BAAEAIAAAP8AAAACBISPCQUAOw==";
  GdkPixbufAnimation *animation = animationFrom(once);
  EXPECT(animation != nullptr);
  if (animation == nullptr) {
    return;
  }

  View view(animation);
  // Halfway through the first frame's 80ms, still running.
  EXPECT(rn_view_advance_animation(view.get(), 60.0) > 0.0);
  // One pass is 120ms, after which nothing more is due: the tick callback stops
  // on exactly this, which is what keeps a played-out GIF from waking the
  // compositor for the rest of the session.
  EXPECT_NEAR(rn_view_advance_animation(view.get(), 60.0), 0.0, 0.01);
  // And the last frame stays, rather than snapping back to the first.
  EXPECT(view.centre().blue > 200);

  g_object_unref(animation);
}

G_GNUC_END_IGNORE_DEPRECATIONS
