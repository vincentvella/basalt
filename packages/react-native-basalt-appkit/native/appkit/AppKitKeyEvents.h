// An NSEvent as a key combination, in the vocabulary core/KeyEvents.h defines.
//
// Separate from AppKitFocus because it is a translation and nothing else: no
// registry, no focus, no decision. The three hosts each need one of these and
// they share no code, which is exactly why the *names* they translate into are
// constants in core rather than literals here.

#pragma once

#include "KeyEvents.h"

#include <optional>

#ifdef __OBJC__
@class NSEvent;
#endif

namespace basalt {

#ifdef __OBJC__
// The combination this key press represents, or nothing for a press with no key
// to speak of -- a bare modifier, or a dead key mid-composition.
std::optional<KeyCombination> keyCombinationFrom(NSEvent *event);
#endif

} // namespace basalt
