// What kind of text a field expects, as Windows wants to be told it.
//
// `keyboardType` is a hint rather than a restriction: on a phone it picks a soft
// keyboard, and on a desktop there is no soft keyboard to pick. Each toolkit has
// somewhere to put the hint anyway, because an IME and a touch keyboard both
// want it: GTK has `GtkInputPurpose`, and Windows has an *input scope*, set on a
// window with `SetInputScope`. The touch keyboard reads it, so a Surface typing
// into an `email-address` field gets the keyboard with the @ on it, and a
// text-services IME reads it too.
//
// AppKit has nothing of the kind, which is why `backlog/platform-macos.md`
// records the prop as reported rather than honoured there. This host can honour
// it, so it does.
//
// The mapping is from the *name* `core/TextChecking.h` prints -- the one React
// Native writes in a stylesheet -- rather than from the enum, so this header
// carries no React Native and can be tested beside the other view-layer tables.
//
// `IS_DEFAULT` means "no particular kind", which is what an unmapped keyboard
// type and the default one both are.

#pragma once

#include <windows.h>

// `SetInputScope` and the `IS_` family. Its own header rather than part of
// windows.h, and it is the text-services one: the function lives in msctf and
// needs InputScope.h for the enum.
#include <inputscope.h>

#include <string>

namespace basalt::win32 {

// The input scope for a `keyboardType` name.
//
// Several names land on the same scope because Windows draws no difference, and
// the ones that are iOS's own vocabulary -- `twitter`, `name-phone-pad` -- land
// on the nearest kind rather than on nothing: a field asking for a Twitter
// keyboard is asking for text, and saying `IS_DEFAULT` for it would be as
// accurate and less useful.
inline InputScope inputScopeForKeyboardType(const std::string &name) {
  if (name == "email-address") {
    return IS_EMAIL_SMTPEMAILADDRESS;
  }
  if (name == "url" || name == "web-search") {
    return IS_URL;
  }
  if (name == "phone-pad" || name == "name-phone-pad") {
    return IS_TELEPHONE_FULLTELEPHONENUMBER;
  }
  if (name == "numeric" || name == "decimal-pad" || name == "numbers-and-punctuation") {
    // A number that may carry a separator or a sign, which is what `numeric`
    // and `decimal-pad` mean. `IS_DIGITS` is digits and nothing else, and is
    // what the two pad types below ask for.
    return IS_NUMBER;
  }
  if (name == "number-pad" || name == "ascii-capable-number-pad") {
    return IS_DIGITS;
  }
  // `default`, `ascii-capable`, `twitter`, `visible-password` and anything a
  // future React Native adds: ordinary text.
  return IS_DEFAULT;
}

} // namespace basalt::win32
