#include "ImageCache.h"

namespace basalt {

void ImageCachePolicy::noteUse(const std::string &uri) {
  const auto it = entries_.find(uri);
  if (it == entries_.end()) {
    return;
  }
  order_.splice(order_.begin(), order_, it->second);
}

std::vector<std::string> ImageCachePolicy::insert(const std::string &uri, size_t bytes) {
  // Replacing what is already there: the same URI decoded again, which happens
  // when a host reloads an image whose contents changed.
  remove(uri);

  order_.push_front(Entry{uri, bytes});
  entries_[uri] = order_.begin();
  bytes_ += bytes;

  std::vector<std::string> evicted;
  // Never the entry just inserted, however large it is -- it is on screen, and
  // dropping it here would decode it again on the next frame, forever.
  while (bytes_ > budget_ && order_.size() > 1) {
    const Entry &oldest = order_.back();
    evicted.push_back(oldest.uri);
    bytes_ -= oldest.bytes;
    entries_.erase(oldest.uri);
    order_.pop_back();
  }
  return evicted;
}

void ImageCachePolicy::remove(const std::string &uri) {
  const auto it = entries_.find(uri);
  if (it == entries_.end()) {
    return;
  }
  bytes_ -= it->second->bytes;
  order_.erase(it->second);
  entries_.erase(it);
}

void ImageCachePolicy::clear() {
  order_.clear();
  entries_.clear();
  bytes_ = 0;
}

} // namespace basalt
