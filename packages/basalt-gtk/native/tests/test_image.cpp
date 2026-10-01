// Tests for the image loader.
//
// A base64 data: URI keeps these self-contained: no file on disk, no network,
// no assets directory to keep in step. The loader answers synchronously for a
// cached URI and through the main loop otherwise, so the async cases pump the
// loop until the callback lands.

#include "TestHarness.h"

#include "GtkImageLoader.h"

#include <sstream>
#include <string>

namespace {

// A 2x1 PNG: one red pixel, one blue. Small enough to inline, real enough to
// decode.
const char *kTinyPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAIAAAABCAIAAAB7QOjdAAAADUlEQVR42mP4zwAE/wEHAAH/PX2MSQAAAABJRU5ErkJggg==";

std::string tinyPngUri() {
  return std::string("data:image/png;base64,") + kTinyPngBase64;
}

// Runs the main loop until `done`, or until a generous number of iterations
// have passed. Returns false on timeout so a hang fails rather than blocks.
bool pumpUntil(const bool &done) {
  for (int i = 0; i < 20000 && !done; ++i) {
    g_main_context_iteration(nullptr, FALSE);
    g_usleep(500);
  }
  return done;
}

} // namespace

TEST(image_loader_decodes_a_data_uri) {
  basalt::GtkImageLoader loader;

  bool done = false;
  GdkTexture *result = nullptr;
  std::string error;

  loader.load(tinyPngUri(), [&](GdkTexture *texture, const std::string &message) {
    result = texture;
    error = message;
    done = true;
  });

  EXPECT(pumpUntil(done));
  EXPECT(error.empty());
  EXPECT(result != nullptr);
  if (result != nullptr) {
    EXPECT_EQ(gdk_texture_get_width(result), 2);
    EXPECT_EQ(gdk_texture_get_height(result), 1);
  }
}

TEST(image_loader_serves_a_second_request_from_cache) {
  basalt::GtkImageLoader loader;

  bool first = false;
  GdkTexture *firstTexture = nullptr;
  loader.load(tinyPngUri(), [&](GdkTexture *texture, const std::string &) {
    firstTexture = texture;
    first = true;
  });
  EXPECT(pumpUntil(first));

  // The second call must answer without going through the main loop at all,
  // which is what stops an <Image> restarting its load on every mutation.
  bool secondCalledSynchronously = false;
  GdkTexture *secondTexture = nullptr;
  loader.load(tinyPngUri(), [&](GdkTexture *texture, const std::string &) {
    secondTexture = texture;
    secondCalledSynchronously = true;
  });

  EXPECT(secondCalledSynchronously);
  EXPECT(secondTexture == firstTexture);
}

TEST(image_loader_reports_a_missing_file) {
  basalt::GtkImageLoader loader;

  bool done = false;
  GdkTexture *result = nullptr;
  std::string error;

  loader.load("/definitely/not/a/real/image.png", [&](GdkTexture *texture, const std::string &message) {
    result = texture;
    error = message;
    done = true;
  });

  EXPECT(pumpUntil(done));
  EXPECT(result == nullptr);
  EXPECT(!error.empty());
}

TEST(image_loader_rejects_an_empty_uri_without_touching_the_loop) {
  basalt::GtkImageLoader loader;

  bool called = false;
  std::string error;
  loader.load("", [&](GdkTexture *texture, const std::string &message) {
    EXPECT(texture == nullptr);
    error = message;
    called = true;
  });

  EXPECT(called);
  EXPECT(!error.empty());
}

TEST(image_loader_reports_undecodable_bytes) {
  basalt::GtkImageLoader loader;

  bool done = false;
  GdkTexture *result = nullptr;
  std::string error;

  // Valid base64, not a valid image.
  loader.load("data:image/png;base64,bm90YW5pbWFnZQ==",
              [&](GdkTexture *texture, const std::string &message) {
                result = texture;
                error = message;
                done = true;
              });

  EXPECT(pumpUntil(done));
  EXPECT(result == nullptr);
  EXPECT(!error.empty());
}
