// The image loader: the cache, the ways a load can fail, and the two lifetimes
// that make this a class rather than a function.
//
// The equivalent of `tests/test_image.cpp` on GTK, which asks the same three
// questions of its loader. What is extra here is the loader outliving its owner
// -- GTK's loader does not have that hazard, because a completion queued on a
// GLib source is cancelled with the source, and AppKit's has the hazard and no
// test.
//
// These run with no UI thread installed, which is deliberate and is the
// configuration `load` detects: with nothing to marshal back to it stays on one
// thread, so a completion has run by the time `load` returns and nothing here
// has to wait for anything.

#include "TestHarness.h"

#include "Win32ImageLoader.h"
#include "Win32Snapshot.h"
#include "RnWin32View.h"

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using basalt::win32::RnWin32Image;
using basalt::win32::RnWin32View;
using basalt::win32::Win32ImageLoader;

namespace {

// A real PNG on disk, written by the snapshot encoder rather than checked in.
// Cheaper than a fixture and it fails if either half of the image path breaks.
std::string writeTemporaryPng(const char *name) {
  auto source = std::make_unique<RnWin32View>(1);
  source->setFrame(0, 0, 20, 10);
  source->setBackgroundColor(0.0f, 0.5f, 1.0f, 1.0f, true);

  const char *temp = std::getenv("TEMP");
  std::string path = std::string(temp != nullptr ? temp : ".") + "\\" + name;
  if (!basalt::win32::writeSnapshot(*source, path)) {
    return {};
  }
  return path;
}

// file:// is what an app's bundled asset arrives as, and what
// core/ImageBytes.cpp understands without a network.
std::string fileUri(const std::string &path) {
  std::string uri = "file:///" + path;
  for (char &c : uri) {
    if (c == '\\') {
      c = '/';
    }
  }
  return uri;
}

} // namespace

TEST(image_loader_decodes_a_file_uri) {
  const std::string path = writeTemporaryPng("basalt_loader_one.png");
  EXPECT(!path.empty());

  Win32ImageLoader loader;
  std::shared_ptr<RnWin32Image> loaded;
  std::string error;
  loader.load(fileUri(path), [&](std::shared_ptr<RnWin32Image> image, const std::string &why) {
    loaded = std::move(image);
    error = why;
  });

  EXPECT(loaded != nullptr);
  EXPECT_EQ(error, std::string{});
  if (loaded != nullptr) {
    EXPECT_EQ(loaded->width(), 20u);
    EXPECT_EQ(loaded->height(), 10u);
  }
  std::remove(path.c_str());
}

TEST(image_loader_serves_a_second_request_from_cache) {
  const std::string path = writeTemporaryPng("basalt_loader_two.png");
  EXPECT(!path.empty());
  const std::string uri = fileUri(path);

  Win32ImageLoader loader;
  std::shared_ptr<RnWin32Image> first;
  loader.load(uri, [&](std::shared_ptr<RnWin32Image> image, const std::string &) {
    first = std::move(image);
  });
  EXPECT(first != nullptr);

  // Deleted before the second request, so a loader that went back to the disk
  // would fail. The same object coming back is what says it did not.
  std::remove(path.c_str());

  std::shared_ptr<RnWin32Image> second;
  loader.load(uri, [&](std::shared_ptr<RnWin32Image> image, const std::string &) {
    second = std::move(image);
  });
  EXPECT(second != nullptr);
  EXPECT(first == second);
}

TEST(image_loader_reports_a_missing_file) {
  Win32ImageLoader loader;
  std::shared_ptr<RnWin32Image> loaded;
  std::string error;
  loader.load("file:///c:/nothing/here/at/all.png",
              [&](std::shared_ptr<RnWin32Image> image, const std::string &why) {
                loaded = std::move(image);
                error = why;
              });

  EXPECT(loaded == nullptr);
  EXPECT(!error.empty());
}

TEST(image_loader_rejects_an_empty_uri) {
  Win32ImageLoader loader;
  bool called = false;
  std::shared_ptr<RnWin32Image> loaded;
  loader.load("", [&](std::shared_ptr<RnWin32Image> image, const std::string &) {
    called = true;
    loaded = std::move(image);
  });

  // Answered rather than ignored: a caller that never hears back cannot tell
  // the difference between a slow load and a dropped one.
  EXPECT(called);
  EXPECT(loaded == nullptr);
}

TEST(image_loader_reports_bytes_that_are_not_an_image) {
  const char *temp = std::getenv("TEMP");
  const std::string path = std::string(temp != nullptr ? temp : ".") + "\\basalt_loader_junk.png";
  if (FILE *file = std::fopen(path.c_str(), "wb")) {
    std::fputs("not a png at all", file);
    std::fclose(file);
  }

  Win32ImageLoader loader;
  std::shared_ptr<RnWin32Image> loaded;
  std::string error;
  loader.load(fileUri(path), [&](std::shared_ptr<RnWin32Image> image, const std::string &why) {
    loaded = std::move(image);
    error = why;
  });

  EXPECT(loaded == nullptr);
  EXPECT(!error.empty());
  std::remove(path.c_str());
}

TEST(image_loader_clears_its_cache) {
  const std::string path = writeTemporaryPng("basalt_loader_three.png");
  EXPECT(!path.empty());
  const std::string uri = fileUri(path);

  Win32ImageLoader loader;
  std::shared_ptr<RnWin32Image> first;
  loader.load(uri, [&](std::shared_ptr<RnWin32Image> image, const std::string &) {
    first = std::move(image);
  });
  EXPECT(first != nullptr);

  loader.clearCache();

  std::shared_ptr<RnWin32Image> second;
  loader.load(uri, [&](std::shared_ptr<RnWin32Image> image, const std::string &) {
    second = std::move(image);
  });
  EXPECT(second != nullptr);
  // A different object: the cache really was dropped and the file read again.
  EXPECT(first != second);

  std::remove(path.c_str());
}

TEST(image_loader_state_outlives_the_loader) {
  // The hazard this class exists to avoid. A load's completion holds a shared
  // reference to the loader's state rather than to the loader, so a loader
  // destroyed between the two is not a use-after-free.
  //
  // Synchronous here, so this cannot catch the race directly -- what it does
  // catch is the shape: destroying a loader that has loaded must not disturb
  // an image its caller is still holding, which would be the symptom if the
  // cache owned rather than shared.
  const std::string path = writeTemporaryPng("basalt_loader_four.png");
  EXPECT(!path.empty());

  std::shared_ptr<RnWin32Image> survivor;
  {
    Win32ImageLoader loader;
    loader.load(fileUri(path), [&](std::shared_ptr<RnWin32Image> image, const std::string &) {
      survivor = std::move(image);
    });
  }

  EXPECT(survivor != nullptr);
  if (survivor != nullptr) {
    EXPECT_EQ(survivor->width(), 20u);
  }
  std::remove(path.c_str());
}
