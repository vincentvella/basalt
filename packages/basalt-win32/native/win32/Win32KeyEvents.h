// A Win32 key press as a key combination, in core/KeyEvents.h's vocabulary.
//
// The third of three translations, and the one that has to work hardest.
// `WM_KEYDOWN` carries a virtual key and nothing else: no modifier state, and no
// character. So the modifiers come from `GetKeyState` and the character from the
// keyboard layout, where the other two hosts are handed both.

#pragma once

#include "KeyEvents.h"

#include <optional>

namespace basalt {

// Nothing for a press with no key to speak of: a bare modifier, or a dead key.
// Reads the modifier state itself, because the message does not carry it.
std::optional<KeyCombination> keyCombinationFrom(unsigned int virtualKey);

} // namespace basalt
