#include "TestKeys.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace basalt {
namespace {

std::vector<std::string> split(const std::string &text, char separator) {
  std::vector<std::string> parts;
  std::string current;
  for (const char character : text) {
    if (character == separator) {
      parts.push_back(current);
      current.clear();
      continue;
    }
    current += character;
  }
  parts.push_back(current);
  return parts;
}

} // namespace

KeyCombination parseKeyPress(const std::string &spec) {
  KeyCombination pressed;
  if (spec.empty()) {
    return pressed;
  }

  // The key first, then modifiers. Split on `+` from the left and take the first
  // piece as the key, so a literal `+` as the key is `"+"` or `"++shift"` -- the
  // first piece being empty is the one case where an empty piece means something.
  const std::vector<std::string> pieces = split(spec, '+');
  pressed.key = pieces.front();
  for (size_t i = 1; i < pieces.size(); i++) {
    const std::string &modifier = pieces[i];
    if (modifier == "alt") {
      pressed.modifiers.alt = true;
    } else if (modifier == "ctrl") {
      pressed.modifiers.ctrl = true;
    } else if (modifier == "meta") {
      pressed.modifiers.meta = true;
    } else if (modifier == "shift") {
      pressed.modifiers.shift = true;
    } else if (modifier.empty() && pressed.key.empty()) {
      // `"++shift"`: the key is a literal plus, and this empty piece is it.
      pressed.key = "+";
    }
    // Anything else is ignored rather than refused. A typo in a modifier name
    // gives a combination that matches nothing, which the scenario reports as a
    // shortcut that did not fire -- and that is the honest symptom of a typo.
  }
  return pressed;
}

std::vector<KeyCombination> scriptedKeyPresses() {
  const char *value = std::getenv(kTestKeyVar);
  if (value == nullptr || *value == '\0') {
    return {};
  }
  std::vector<KeyCombination> presses;
  for (const std::string &spec : split(std::string(value), ';')) {
    if (spec.empty()) {
      continue;
    }
    KeyCombination pressed = parseKeyPress(spec);
    if (pressed.key.empty()) {
      continue;
    }
    presses.push_back(std::move(pressed));
  }
  return presses;
}

} // namespace basalt
