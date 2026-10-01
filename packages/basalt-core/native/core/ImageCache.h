// What a decoded image cache keeps, and what it drops.
//
// All three hosts cache decoded images by URI in an `unordered_map` that
// nothing ever removes from. That is fine for the demo, where a handful of
// images load once, and wrong for the case the cache exists for: a long list
// of remote images, scrolled, where every one ever seen stays in memory until
// the process exits.
//
// ## Why the policy is here and the pixels are not
//
// The three hosts hold three unrelated things -- a `GdkTexture`, a `CGImage`,
// an `RnWin32Image` -- and none of them can be described in core without
// dragging a toolkit in with it. What *can* be shared is the decision: which
// URI to drop next, and when. So this tracks keys and sizes, answers with the
// URIs to release, and never sees a pixel.
//
// It also means the policy is testable without a display, which is the same
// reason core/ScrollIndicator.h and core/ScrollBounds.h are shaped this way.
//
// ## Least recently used
//
// The alternative is least recently *added*, which is cheaper and wrong for
// exactly the case this exists for: scrolling a list back up should not have
// to re-decode the rows that are about to be on screen again. A use is a hit
// or an insert, and both move an entry to the front.
//
// Not thread-safe, and not meant to be: every host calls this from the thread
// that owns its views, which is where the decode is handed back to.

#pragma once

#include <cstddef>
#include <list>
#include <string>
#include <unordered_map>
#include <vector>

namespace basalt {

// Roughly a dozen full-screen images on a 1080p display: 1920 x 1080 x 4 bytes
// is 8.3MB, so this is about a dozen of them, or a great many thumbnails.
//
// A number rather than a fraction of anything, because the three desktops do
// not agree on how to ask how much memory there is, and a cache whose size
// depended on the machine would make a leak reproduce on one and not another.
inline constexpr size_t kImageCacheBudgetBytes = 96u * 1024u * 1024u;

class ImageCachePolicy {
 public:
  explicit ImageCachePolicy(size_t budgetBytes = kImageCacheBudgetBytes)
      : budget_(budgetBytes) {}

  // Records that a cached image was used, moving it to the front. Does nothing
  // for a URI this does not know, which is what a hit on a host that cached
  // something before this existed looks like.
  void noteUse(const std::string &uri);

  // Records a newly decoded image, and answers with the URIs whose images
  // should now be released -- oldest first, and never the one just inserted.
  //
  // An image larger than the whole budget is kept rather than refused: it is
  // on screen, and a cache that evicted it immediately would decode it again
  // on the next frame forever.
  std::vector<std::string> insert(const std::string &uri, size_t bytes);

  // Drops one entry, for a host that released an image for its own reasons.
  void remove(const std::string &uri);

  void clear();

  size_t bytes() const { return bytes_; }
  size_t count() const { return entries_.size(); }
  size_t budget() const { return budget_; }

 private:
  struct Entry {
    std::string uri;
    size_t bytes{0};
  };

  // Front is most recently used. A list so that moving an entry is a splice
  // and the iterators the map holds stay valid.
  std::list<Entry> order_;
  std::unordered_map<std::string, std::list<Entry>::iterator> entries_;
  size_t bytes_{0};
  size_t budget_;
};

} // namespace basalt
