#include "AppKitKeyEvents.h"

#import <AppKit/AppKit.h>

#include <string>

namespace basalt {
namespace {

// AppKit reports the arrows and the navigation block as characters in the
// private-use area, which is why these are compared against
// `charactersIgnoringModifiers` rather than against a key code: a key code is
// keyboard-layout dependent and these are not.
const char *specialNameFor(unichar character) {
  switch (character) {
  case NSUpArrowFunctionKey:
    return kKeyArrowUp;
  case NSDownArrowFunctionKey:
    return kKeyArrowDown;
  case NSLeftArrowFunctionKey:
    return kKeyArrowLeft;
  case NSRightArrowFunctionKey:
    return kKeyArrowRight;
  case NSPageUpFunctionKey:
    return kKeyPageUp;
  case NSPageDownFunctionKey:
    return kKeyPageDown;
  case NSHomeFunctionKey:
    return kKeyHome;
  case NSEndFunctionKey:
    return kKeyEnd;
  // Forward delete. The key most keyboards label "delete" is backspace, below.
  case NSDeleteFunctionKey:
    return kKeyDelete;
  case NSCarriageReturnCharacter:
  case NSEnterCharacter:
    return kKeyEnter;
  case NSTabCharacter:
  // Shift-Tab arrives as its own character rather than as Tab with a modifier.
  // Reported as Tab with shift set, because that is what a browser reports and
  // what an app will have bound.
  case NSBackTabCharacter:
    return kKeyTab;
  case 0x1B:
    return kKeyEscape;
  case NSDeleteCharacter:
    return kKeyBackspace;
  default:
    return nullptr;
  }
}

} // namespace

std::optional<KeyCombination> keyCombinationFrom(NSEvent *event) {
  if (event == nil) {
    return std::nullopt;
  }

  KeyModifiers modifiers;
  const NSEventModifierFlags flags = event.modifierFlags;
  modifiers.alt = (flags & NSEventModifierFlagOption) != 0;
  modifiers.ctrl = (flags & NSEventModifierFlagControl) != 0;
  // Command is `meta`, as it is in a browser on this platform and in
  // react-native-macos. An app binding Cmd+Z writes metaKey.
  modifiers.meta = (flags & NSEventModifierFlagCommand) != 0;
  modifiers.shift = (flags & NSEventModifierFlagShift) != 0;

  NSString *bare = event.charactersIgnoringModifiers;
  if (bare.length > 0) {
    if (const char *special = specialNameFor([bare characterAtIndex:0])) {
      return KeyCombination{std::string(special), modifiers};
    }
  }

  // A character, with its case. `characters` applies Shift, so Shift+A is "A"
  // and its shift modifier is also set -- both true, as in a browser. It also
  // applies Option, so Option+A is "å" on a US layout, which is again what a
  // browser reports and what the person actually typed.
  //
  // Not `charactersIgnoringModifiers`, which would report "a" for Shift+A and
  // make the two indistinguishable to an app that binds by character.
  NSString *typed = event.characters;
  if (typed.length == 0) {
    // A bare modifier, or a dead key part-way through composing. Neither is a
    // combination and neither should be offered to an app as one.
    return std::nullopt;
  }
  // Control turns a letter into its control character -- Ctrl+A is 0x01 -- which
  // is not a name anybody binds. The unmodified character is what an app means.
  if (modifiers.ctrl && bare.length > 0) {
    typed = bare;
  }
  return KeyCombination{std::string(typed.UTF8String), modifiers};
}

} // namespace basalt
