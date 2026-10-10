#include "TextSelection.h"

#include "PlatformServices.h"

#include <algorithm>
#include <utility>
#include <cmath>

namespace basalt {

void TextSelection::press(facebook::react::Tag tag, int index, facebook::react::Point at) {
  // A press always ends the previous selection, whether or not this one becomes
  // a selection of its own: clicking somewhere is how a person dismisses a
  // highlight, and that has to happen on the press rather than on the release
  // or the old highlight survives the click that was meant to end it.
  tag_ = tag;
  anchor_ = index;
  focus_ = index;
  origin_ = at;
  pressed_ = tag != 0;
  claimed_ = false;
}

bool TextSelection::moveTo(int index, facebook::react::Point at) {
  if (!pressed_) {
    return false;
  }

  if (!claimed_) {
    const double dx = static_cast<double>(at.x) - static_cast<double>(origin_.x);
    const double dy = static_cast<double>(at.y) - static_cast<double>(origin_.y);
    if (std::sqrt(dx * dx + dy * dy) < kTextSelectionThreshold) {
      // Still a tap as far as anybody can tell.
      return false;
    }
    claimed_ = true;
    focus_ = index;
    // True exactly once per gesture, which is what the caller uses to cancel
    // the touch sequence it had been reporting.
    return true;
  }

  focus_ = index;
  return false;
}

void TextSelection::release() {
  pressed_ = false;
}

void TextSelection::clear() {
  tag_ = 0;
  anchor_ = 0;
  focus_ = 0;
  pressed_ = false;
  claimed_ = false;
}

TextSelectionRange TextSelection::range() const {
  if (!claimed_) {
    return {};
  }
  const int start = std::min(anchor_, focus_);
  const int end = std::max(anchor_, focus_);
  return TextSelectionRange{start, end - start};
}

// --- Copying it ---------------------------------------------------------------

namespace {

std::function<std::string()> &provider() {
  static std::function<std::string()> value;
  return value;
}

} // namespace

void setSelectedTextProvider(std::function<std::string()> value) {
  provider() = std::move(value);
}

std::string selectedText() {
  const std::function<std::string()> &ask = provider();
  return ask ? ask() : std::string{};
}

bool copySelectedText() {
  const std::string text = selectedText();
  if (text.empty()) {
    return false;
  }
  setClipboardText(text);
  return true;
}

} // namespace basalt
