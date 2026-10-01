#include "PullToRefresh.h"

namespace basalt {

using facebook::react::Tag;

void PullToRefreshTracker::attach(Tag scrollTag, Tag controlTag) {
  if (scrollTag == 0 || controlTag == 0) {
    return;
  }
  byScroll_[scrollTag].control = controlTag;
  scrollByControl_[controlTag] = scrollTag;
}

void PullToRefreshTracker::forget(Tag tag) {
  if (const auto asControl = scrollByControl_.find(tag); asControl != scrollByControl_.end()) {
    byScroll_.erase(asControl->second);
    scrollByControl_.erase(asControl);
    return;
  }
  if (const auto asScroll = byScroll_.find(tag); asScroll != byScroll_.end()) {
    scrollByControl_.erase(asScroll->second.control);
    byScroll_.erase(asScroll);
  }
}

Tag PullToRefreshTracker::controlFor(Tag scrollTag) const {
  const auto found = byScroll_.find(scrollTag);
  return found != byScroll_.end() ? found->second.control : 0;
}

bool PullToRefreshTracker::pull(Tag scrollTag, double amount) {
  const auto found = byScroll_.find(scrollTag);
  if (found == byScroll_.end()) {
    return false;
  }
  Entry &entry = found->second;
  if (amount > 0.0) {
    entry.pulled += amount;
  }
  if (entry.fired || entry.pulled < kPullToRefreshThreshold) {
    return false;
  }
  entry.fired = true;
  return true;
}

void PullToRefreshTracker::release(Tag scrollTag) {
  const auto found = byScroll_.find(scrollTag);
  if (found == byScroll_.end()) {
    return;
  }
  found->second.pulled = 0.0;
  found->second.fired = false;
}

} // namespace basalt
