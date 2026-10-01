#include "DebuggingOverlay.h"

#include <cstdint>

namespace basalt {

namespace {

// DevTools' own blue, which React Native draws an inspected element in on iOS
// and Android. Hard-coded rather than themed: the point of the colour is that
// it is recognisably DevTools' rather than the app's.
constexpr float kInspectorBlue[4] = {0.102F, 0.624F, 0.980F, 0.45F};

// The colour a trace update falls back to when DevTools sent none.
constexpr float kTraceFallback[4] = {0.0F, 0.78F, 0.33F, 1.0F};

float numberAt(const folly::dynamic &object, const char *key) {
  const auto *value = object.get_ptr(key);
  return value != nullptr && value->isNumber() ? static_cast<float>(value->asDouble()) : 0.0F;
}

// A processed colour: React Native's JavaScript sends these as one integer in
// 0xAARRGGBB, which is what `processColor` produces and what every other colour
// crossing this boundary looks like.
bool readProcessedColor(const folly::dynamic &object, const char *key, float out[4]) {
  const auto *value = object.get_ptr(key);
  if (value == nullptr || !value->isNumber()) {
    return false;
  }
  const auto packed = static_cast<uint32_t>(value->asInt());
  out[0] = static_cast<float>((packed >> 16) & 0xFFU) / 255.0F;
  out[1] = static_cast<float>((packed >> 8) & 0xFFU) / 255.0F;
  out[2] = static_cast<float>(packed & 0xFFU) / 255.0F;
  out[3] = static_cast<float>((packed >> 24) & 0xFFU) / 255.0F;
  return true;
}

void readRectangle(const folly::dynamic &source, Highlight &highlight) {
  highlight.x = numberAt(source, "x");
  highlight.y = numberAt(source, "y");
  highlight.width = numberAt(source, "width");
  highlight.height = numberAt(source, "height");
}

// The first argument, as a list. A command's payload is an array of arguments
// and every one of these takes a single array.
const folly::dynamic *firstArray(const folly::dynamic &args) {
  if (args.isArray() && !args.empty() && args[0].isArray()) {
    return &args[0];
  }
  // Also accepts the list itself, which is what a caller that unwrapped the
  // arguments already would send -- and what the tests find easier to write.
  return args.isArray() ? &args : nullptr;
}

} // namespace

std::vector<Highlight> parseTraceUpdates(const folly::dynamic &args) {
  std::vector<Highlight> highlights;
  const folly::dynamic *list = firstArray(args);
  if (list == nullptr) {
    return highlights;
  }
  for (const auto &entry : *list) {
    if (!entry.isObject()) {
      continue;
    }
    Highlight highlight;
    const auto *rectangle = entry.get_ptr("rectangle");
    if (rectangle == nullptr || !rectangle->isObject()) {
      continue;
    }
    readRectangle(*rectangle, highlight);
    if (!readProcessedColor(entry, "color", highlight.color)) {
      for (int i = 0; i < 4; i++) {
        highlight.color[i] = kTraceFallback[i];
      }
    }
    highlight.filled = false;
    highlights.push_back(highlight);
  }
  return highlights;
}

std::vector<Highlight> parseElementHighlights(const folly::dynamic &args) {
  std::vector<Highlight> highlights;
  const folly::dynamic *list = firstArray(args);
  if (list == nullptr) {
    return highlights;
  }
  for (const auto &entry : *list) {
    if (!entry.isObject()) {
      continue;
    }
    Highlight highlight;
    readRectangle(entry, highlight);
    for (int i = 0; i < 4; i++) {
      highlight.color[i] = kInspectorBlue[i];
    }
    highlight.filled = true;
    highlights.push_back(highlight);
  }
  return highlights;
}

} // namespace basalt
