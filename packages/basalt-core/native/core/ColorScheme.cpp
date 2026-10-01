#include "ColorScheme.h"

#include <mutex>
#include <unordered_map>

namespace basalt {

namespace {

struct Registry {
  std::mutex mutex;
  bool hasOverride{false};
  ColorScheme override{ColorScheme::Light};
  int nextToken{1};
  std::unordered_map<int, ColorSchemeObserver> observers;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

} // namespace

const char *colorSchemeName(ColorScheme scheme) {
  return scheme == ColorScheme::Dark ? "dark" : "light";
}

ColorScheme effectiveColorScheme() {
  {
    Registry &state = registry();
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (state.hasOverride) {
      return state.override;
    }
  }
  // Outside the lock: a platform's answer may take a trip to the window server,
  // and holding the lock across that would serialise every reader behind it.
  return systemColorScheme();
}

void setColorSchemeOverride(const std::string &scheme) {
  if (scheme.empty()) {
    clearColorSchemeOverride();
    return;
  }

  {
    Registry &state = registry();
    const std::lock_guard<std::mutex> lock(state.mutex);
    state.hasOverride = true;
    state.override = scheme == "dark" ? ColorScheme::Dark : ColorScheme::Light;
  }
  notifyColorSchemeChanged();
}

void clearColorSchemeOverride() {
  {
    Registry &state = registry();
    const std::lock_guard<std::mutex> lock(state.mutex);
    if (!state.hasOverride) {
      return;
    }
    state.hasOverride = false;
  }
  notifyColorSchemeChanged();
}

int addColorSchemeObserver(ColorSchemeObserver observer) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  const int token = state.nextToken++;
  state.observers.emplace(token, std::move(observer));
  return token;
}

void removeColorSchemeObserver(int token) {
  Registry &state = registry();
  const std::lock_guard<std::mutex> lock(state.mutex);
  state.observers.erase(token);
}

void notifyColorSchemeChanged() {
  const ColorScheme scheme = effectiveColorScheme();

  // Copied under the lock and called outside it: an observer emits into
  // JavaScript, which can re-enter here -- `Appearance.setColorScheme` called
  // from a change handler is a perfectly ordinary thing for an app to do.
  std::vector<ColorSchemeObserver> observers;
  {
    Registry &state = registry();
    const std::lock_guard<std::mutex> lock(state.mutex);
    observers.reserve(state.observers.size());
    for (const auto &[token, observer] : state.observers) {
      (void)token;
      observers.push_back(observer);
    }
  }

  for (const auto &observer : observers) {
    observer(scheme);
  }
}

} // namespace basalt
