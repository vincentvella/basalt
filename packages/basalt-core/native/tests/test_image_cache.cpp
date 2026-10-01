// Which decoded image gets dropped, and when.
//
// The three hosts hold three unrelated texture types and none of them can be
// described here, so what is tested is the decision rather than the pixels --
// see core/ImageCache.h.

#include "TestHarness.h"

#include "ImageCache.h"

#include <sstream>

using basalt::ImageCachePolicy;

TEST(image_cache_keeps_what_fits) {
  ImageCachePolicy cache(1000);
  EXPECT(cache.insert("a", 400).empty());
  EXPECT(cache.insert("b", 400).empty());
  EXPECT_EQ((long)cache.count(), 2L);
  EXPECT_EQ((long)cache.bytes(), 800L);
}

TEST(image_cache_evicts_the_oldest_when_it_does_not) {
  ImageCachePolicy cache(1000);
  cache.insert("a", 400);
  cache.insert("b", 400);

  const auto evicted = cache.insert("c", 400);
  EXPECT_EQ((long)evicted.size(), 1L);
  EXPECT(evicted[0] == "a");
  EXPECT_EQ((long)cache.count(), 2L);
  EXPECT_EQ((long)cache.bytes(), 800L);
}

// The whole reason for least-recently-*used* rather than least recently added:
// scrolling back up should not re-decode the rows about to be on screen.
TEST(image_cache_a_use_saves_an_entry) {
  ImageCachePolicy cache(1000);
  cache.insert("a", 400);
  cache.insert("b", 400);
  cache.noteUse("a");

  const auto evicted = cache.insert("c", 400);
  EXPECT_EQ((long)evicted.size(), 1L);
  // `b` is now the oldest, because `a` was touched after it went in.
  EXPECT(evicted[0] == "b");
}

TEST(image_cache_evicts_as_many_as_it_takes) {
  ImageCachePolicy cache(1000);
  cache.insert("a", 300);
  cache.insert("b", 300);
  cache.insert("c", 300);

  const auto evicted = cache.insert("big", 900);
  EXPECT_EQ((long)evicted.size(), 3L);
  EXPECT(evicted[0] == "a");
  EXPECT(evicted[2] == "c");
  EXPECT_EQ((long)cache.count(), 1L);
}

// On screen and larger than the whole budget. Keeping it is wrong in one way
// and evicting it is wrong in a worse one: it would be decoded again on the
// next frame, and every frame after that.
TEST(image_cache_keeps_an_image_larger_than_the_budget) {
  ImageCachePolicy cache(1000);
  const auto evicted = cache.insert("huge", 5000);
  EXPECT(evicted.empty());
  EXPECT_EQ((long)cache.count(), 1L);

  // And the next insert clears it out rather than leaving both.
  const auto next = cache.insert("small", 100);
  EXPECT_EQ((long)next.size(), 1L);
  EXPECT(next[0] == "huge");
}

TEST(image_cache_reinserting_replaces_rather_than_doubling) {
  ImageCachePolicy cache(1000);
  cache.insert("a", 400);
  cache.insert("a", 600);
  EXPECT_EQ((long)cache.count(), 1L);
  EXPECT_EQ((long)cache.bytes(), 600L);
}

TEST(image_cache_remove_and_clear_free_their_bytes) {
  ImageCachePolicy cache(1000);
  cache.insert("a", 400);
  cache.insert("b", 300);

  cache.remove("a");
  EXPECT_EQ((long)cache.count(), 1L);
  EXPECT_EQ((long)cache.bytes(), 300L);

  // Removing what is not there is not an error: a host may release an image
  // this never saw.
  cache.remove("nothing");
  EXPECT_EQ((long)cache.count(), 1L);

  cache.clear();
  EXPECT_EQ((long)cache.count(), 0L);
  EXPECT_EQ((long)cache.bytes(), 0L);
}

TEST(image_cache_a_use_of_something_absent_is_harmless) {
  ImageCachePolicy cache(1000);
  cache.insert("a", 400);
  cache.noteUse("never-seen");
  EXPECT_EQ((long)cache.count(), 1L);
}
