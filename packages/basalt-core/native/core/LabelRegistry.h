// Which view a `nativeID` names, and which views are waiting to be labelled by
// one.
//
// `accessibilityLabelledBy` names other views by their `nativeID` -- "the label
// for this field is that text over there" -- and nothing else in this project
// needs to look a view up that way: the registry in core/MountingWalk.h is keyed
// by tag, and the drag-and-drop code walks *up* from a hit test rather than
// searching for an id. So this is the missing half, and it is shared because
// both hosts need exactly the same bookkeeping and the ordering problem below is
// not host-specific at all.
//
// ## The ordering problem
//
// A view can name an id that does not exist yet. Fabric mounts in tree order, so
// a field labelled by the text *after* it is mounted before its label, and a
// resolution done once at mount time would find nothing and stay wrong. The
// relation also has to come apart again: a label that is unmounted leaves a
// dangling reference, and a screen reader following it would read a view that is
// no longer on screen.
//
// So this holds both sides and answers which relations have *changed*, which is
// what a host applies. Asking after every transaction is cheap and is right
// whatever order things mounted in.
//
// Header-only, like core/Backface.h, and free of React Native: it takes tags and
// strings.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace basalt {

class LabelRegistry {
 public:
  using Tag = std::int32_t;

  // One view's relation, resolved: the tags its `accessibilityLabelledBy` ids
  // name, in the order the app wrote them, and empty when none of them resolve.
  struct Resolved {
    Tag tag;
    std::vector<Tag> labels;
  };

  // This view's `nativeID`. Empty forgets it, which is what a view that drops
  // the prop means.
  void setNativeId(Tag tag, const std::string &nativeId) {
    const auto previous = nativeIdOf_.find(tag);
    if (previous != nativeIdOf_.end()) {
      if (previous->second == nativeId) {
        return;
      }
      // Only if it is still ours: two views with the same nativeID is an app
      // bug, and the second one to claim it owns it until it goes away.
      const auto owner = tagOfNativeId_.find(previous->second);
      if (owner != tagOfNativeId_.end() && owner->second == tag) {
        tagOfNativeId_.erase(owner);
      }
      nativeIdOf_.erase(previous);
    }
    if (nativeId.empty()) {
      return;
    }
    nativeIdOf_[tag] = nativeId;
    tagOfNativeId_[nativeId] = tag;
  }

  // This view's `accessibilityLabelledBy`. Empty forgets it.
  void setLabelledBy(Tag tag, std::vector<std::string> ids) {
    if (ids.empty()) {
      labelledBy_.erase(tag);
      return;
    }
    labelledBy_[tag] = std::move(ids);
  }

  // A view that has gone away, on either side of a relation.
  void forget(Tag tag) {
    setNativeId(tag, "");
    labelledBy_.erase(tag);
    applied_.erase(tag);
  }

  // Nothing to ask about: no view wants a relation, and none has one applied.
  //
  // Both halves matter. A host that checked only the first would skip the
  // transaction in which the last relation went away, and the relation it had
  // already applied would stay on the widget for the rest of the screen's life.
  bool empty() const { return labelledBy_.empty() && applied_.empty(); }

  // The relations whose resolution has changed since the last call, and nothing
  // else: applying an unchanged relation again would tell assistive technology
  // that something happened when nothing did.
  //
  // An id that names no view yet resolves to nothing, and the view is reported
  // again when the id turns up. A relation whose labels have all gone away is
  // reported with an empty list, which is a host's cue to reset it.
  std::vector<Resolved> changes() {
    std::vector<Resolved> changed;
    for (const auto &[tag, ids] : labelledBy_) {
      std::vector<Tag> labels;
      labels.reserve(ids.size());
      for (const std::string &id : ids) {
        const auto found = tagOfNativeId_.find(id);
        if (found != tagOfNativeId_.end() && found->second != tag) {
          labels.push_back(found->second);
        }
      }
      const auto previous = applied_.find(tag);
      if (previous != applied_.end() && previous->second == labels) {
        continue;
      }
      applied_[tag] = labels;
      changed.push_back(Resolved{tag, std::move(labels)});
    }

    // A view that stopped asking: its relation was applied and is no longer
    // wanted, so it is reported empty once and then forgotten.
    for (auto it = applied_.begin(); it != applied_.end();) {
      if (labelledBy_.find(it->first) != labelledBy_.end()) {
        ++it;
        continue;
      }
      if (!it->second.empty()) {
        changed.push_back(Resolved{it->first, {}});
      }
      it = applied_.erase(it);
    }
    return changed;
  }

 private:
  std::unordered_map<Tag, std::string> nativeIdOf_;
  std::unordered_map<std::string, Tag> tagOfNativeId_;
  std::unordered_map<Tag, std::vector<std::string>> labelledBy_;
  // What each view's relation was last reported as, so an unchanged one is not
  // reported twice.
  std::unordered_map<Tag, std::vector<Tag>> applied_;
};

} // namespace basalt
