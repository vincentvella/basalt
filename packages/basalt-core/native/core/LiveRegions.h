// Which views are live regions, and what each of them last said.
//
// `accessibilityLiveRegion` is the prop for a status message: "saved", "three
// results", "offline". It does not change how a view looks or what it reports
// when a screen reader lands on it -- it asks for the new text to be *read out*
// when it changes, without the reader being there. Neither toolkit models that
// as a property of a view: GTK has `gtk_accessible_announce` and AppKit an
// `NSAccessibilityAnnouncementRequested` notification, both of which happen at a
// moment rather than being set.
//
// So honouring the prop is change detection, and that is what this holds: the
// politeness each live region asked for, and the last thing each one said. A
// host collects a region's current text after a transaction and asks here
// whether that is news.
//
// ## Two rules that are the whole of it
//
// **The first text is not news.** Mounting a status line should not read it out:
// nothing changed, the screen just appeared. So the first text a region is seen
// with is remembered silently, and only a later difference is announced. Getting
// this wrong is loud -- every screen with a status message would announce itself
// on arrival.
//
// **The same text twice is not news either.** A view re-renders for every reason
// under the sun, and React Native re-sends identical props on each mutation; a
// region that announced on every transaction would talk over everything else in
// the application.
//
// Header-only, like core/LabelRegistry.h, which solves the sibling problem for
// `accessibilityLabelledBy`. It takes React Native's own politeness enum rather
// than a copy of it: both mounting managers already know React Native, and a
// second three-value enum with a mapping either side of it is two more places to
// put `Polite` where `Assertive` belongs.

#pragma once

#include <react/renderer/components/view/AccessibilityPrimitives.h>

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace basalt {

using LiveRegionPoliteness = facebook::react::AccessibilityLiveRegion;

class LiveRegionRegistry {
 public:
  using Tag = std::int32_t;

  // This view's `accessibilityLiveRegion`. `None` forgets it, which is what a
  // view that drops the prop means.
  void setPoliteness(Tag tag, LiveRegionPoliteness politeness) {
    if (politeness == LiveRegionPoliteness::None) {
      regions_.erase(tag);
      return;
    }
    Region &region = regions_[tag];
    region.politeness = politeness;
  }

  // A view that has gone away. Its last text goes with it, so a region that
  // comes back is new again rather than compared against a screen that is no
  // longer there.
  void forget(Tag tag) { regions_.erase(tag); }

  bool empty() const { return regions_.empty(); }

  // The live regions, in no particular order, for a host to collect text for.
  std::vector<Tag> tags() const {
    std::vector<Tag> tags;
    tags.reserve(regions_.size());
    for (const auto &[tag, region] : regions_) {
      tags.push_back(tag);
    }
    return tags;
  }

  // What this region should say now, or nothing.
  //
  // Nothing covers three cases that are all ordinary: the view is not a live
  // region, this is the first text it has been seen with, or the text has not
  // changed. Empty text is remembered and never announced -- a region that
  // cleared itself has nothing to read out, and announcing "" is silence with a
  // beat of interruption in front of it.
  std::optional<LiveRegionPoliteness> noticed(Tag tag, const std::string &text) {
    const auto found = regions_.find(tag);
    if (found == regions_.end()) {
      return std::nullopt;
    }
    Region &region = found->second;
    const bool first = !region.said.has_value();
    const bool changed = !first && region.said.value() != text;
    region.said = text;
    if (first || !changed || text.empty()) {
      return std::nullopt;
    }
    return region.politeness;
  }

 private:
  struct Region {
    LiveRegionPoliteness politeness{LiveRegionPoliteness::Polite};
    // Absent until the region has been seen once, which is what tells a first
    // sighting from a change.
    std::optional<std::string> said;
  };

  std::unordered_map<Tag, Region> regions_;
};

} // namespace basalt
