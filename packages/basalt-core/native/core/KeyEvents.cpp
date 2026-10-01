#include "KeyEvents.h"

#include <mutex>
#include <unordered_map>
#include <utility>

namespace basalt {
namespace {

struct Registry {
  std::mutex mutex;
  std::unordered_map<facebook::react::Tag, std::vector<KeyCombination>> claims;
  std::function<void(facebook::react::Tag, const KeyCombination &)> listener;
};

Registry &registry() {
  static Registry shared;
  return shared;
}

} // namespace

void setHandledKeys(facebook::react::Tag tag, std::vector<KeyCombination> combinations) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  // Assigned, not merged. A re-render hands over the whole list, and merging
  // would keep a shortcut alive after the app stopped declaring it -- which
  // would look like the shortcut working and be a leak of behaviour.
  state.claims[tag] = std::move(combinations);
}

void clearHandledKeys(facebook::react::Tag tag) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.claims.erase(tag);
}

bool handlesKey(facebook::react::Tag tag, const KeyCombination &pressed) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  const auto it = state.claims.find(tag);
  if (it == state.claims.end()) {
    return false;
  }
  for (const KeyCombination &claimed : it->second) {
    if (claimed == pressed) {
      return true;
    }
  }
  return false;
}

std::optional<facebook::react::Tag> handledBy(
    const std::vector<facebook::react::Tag> &path, const KeyCombination &pressed) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  if (state.claims.empty()) {
    return std::nullopt;
  }
  // In order, which is innermost first: the caller walks outwards from the
  // focused view, so the first claim found is the most specific one.
  for (const facebook::react::Tag tag : path) {
    const auto it = state.claims.find(tag);
    if (it == state.claims.end()) {
      continue;
    }
    for (const KeyCombination &claimed : it->second) {
      if (claimed == pressed) {
        return tag;
      }
    }
  }
  return std::nullopt;
}

std::optional<facebook::react::Tag> handledByAny(const KeyCombination &pressed) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  for (const auto &[tag, combinations] : state.claims) {
    for (const KeyCombination &claimed : combinations) {
      if (claimed == pressed) {
        return tag;
      }
    }
  }
  return std::nullopt;
}

std::size_t handledKeyViewCount() {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  return state.claims.size();
}

void setKeyListener(std::function<void(facebook::react::Tag, const KeyCombination &)> listener) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.listener = std::move(listener);
}

void reportKey(facebook::react::Tag tag, const KeyCombination &pressed) {
  std::function<void(facebook::react::Tag, const KeyCombination &)> listener;
  {
    Registry &state = registry();
    const std::lock_guard<std::mutex> lock(state.mutex);
    listener = state.listener;
  }
  // Outside the lock, as core/DragAndDrop.cpp does and for the same reason: the
  // listener reaches JavaScript, and holding a lock across that invites the
  // deadlock where JavaScript re-enters to change its own claims.
  if (listener) {
    listener(tag, pressed);
  }
}

} // namespace basalt
