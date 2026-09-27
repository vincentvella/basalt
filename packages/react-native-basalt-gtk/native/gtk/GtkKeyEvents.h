// A GDK key press as a key combination, in core/KeyEvents.h's vocabulary.
//
// The GTK counterpart of appkit/AppKitKeyEvents.h, and deliberately not shared
// with it: a keyval is not an NSEvent and the two have nothing in common but the
// names they produce -- which is exactly why those names are constants in core.

#pragma once

#include "KeyEvents.h"

#include <gtk/gtk.h>

#include <optional>

namespace basalt {

// Nothing for a press with no key to speak of: a bare modifier, or a keyval that
// maps to no character and no name.
std::optional<KeyCombination> keyCombinationFrom(guint keyval, GdkModifierType state);

} // namespace basalt
