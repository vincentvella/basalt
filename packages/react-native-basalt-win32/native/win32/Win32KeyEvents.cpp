#include "Win32KeyEvents.h"

#include <windows.h>

#include <cwctype>
#include <string>

namespace basalt {
namespace {

const char *specialNameFor(unsigned int virtualKey) {
  switch (virtualKey) {
    case VK_UP:
      return kKeyArrowUp;
    case VK_DOWN:
      return kKeyArrowDown;
    case VK_LEFT:
      return kKeyArrowLeft;
    case VK_RIGHT:
      return kKeyArrowRight;
    // Named for paging rather than for priority, which is what PRIOR and NEXT
    // are about: VK_PRIOR is Page Up.
    case VK_PRIOR:
      return kKeyPageUp;
    case VK_NEXT:
      return kKeyPageDown;
    case VK_HOME:
      return kKeyHome;
    case VK_END:
      return kKeyEnd;
    case VK_DELETE:
      return kKeyDelete;
    case VK_BACK:
      return kKeyBackspace;
    case VK_RETURN:
      return kKeyEnter;
    // Shift-Tab is VK_TAB with shift held here, unlike the other two hosts where
    // it arrives as its own character or keyval. So there is nothing to fold
    // together -- it already reports as Tab with the modifier set.
    case VK_TAB:
      return kKeyTab;
    case VK_ESCAPE:
      return kKeyEscape;
    case VK_SPACE:
      // A space is a character named " ", and the layout below would give it
      // anyway; named here so all three hosts answer it from the same table.
      return kKeySpace;
    default:
      return nullptr;
  }
}

bool held(int virtualKey) {
  // The high bit is "down now"; the low bit is a toggle and means Caps Lock is
  // on rather than that Shift is held.
  return (GetKeyState(virtualKey) & 0x8000) != 0;
}

} // namespace

std::optional<KeyCombination> keyCombinationFrom(unsigned int virtualKey) {
  KeyModifiers modifiers;
  // VK_MENU is Alt, which is a name from a time when Alt opened menus.
  modifiers.alt = held(VK_MENU);
  modifiers.ctrl = held(VK_CONTROL);
  modifiers.shift = held(VK_SHIFT);
  // Either Windows key is `meta`, as Command is on macOS and Super is on Linux:
  // W3C names the key in that position Meta, so an app binding metaKey presses
  // the same physical key on all three desktops.
  modifiers.meta = held(VK_LWIN) || held(VK_RWIN);

  // A bare modifier is not a combination. Offering one would fire a shortcut on
  // Ctrl alone, and unlike the other two hosts there is no empty character here
  // to detect it by -- the virtual key has to be recognised.
  switch (virtualKey) {
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL:
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU:
    case VK_LWIN:
    case VK_RWIN:
    case VK_CAPITAL:
      return std::nullopt;
    default:
      break;
  }

  if (const char *special = specialNameFor(virtualKey)) {
    return KeyCombination{std::string(special), modifiers};
  }

  // The character this key produces on the current layout. MapVirtualKeyW gives
  // the *unshifted* character and gives letters in upper case, so the case has to
  // be applied here -- where GDK's keyval already carries it and AppKit's
  // `characters` already applies it. Third host, third arrangement, same answer.
  const UINT mapped = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_CHAR);
  if (mapped == 0) {
    return std::nullopt;
  }
  // The top bit means a dead key -- part-way through composing an accent -- which
  // is not a combination any more than a bare modifier is.
  if ((mapped & 0x80000000u) != 0) {
    return std::nullopt;
  }
  wchar_t character = static_cast<wchar_t>(mapped & 0xFFFFu);
  if (!modifiers.shift) {
    // Lowercased rather than left as the layout gave it, so an unshifted A is
    // "a". Only letters change; punctuation is unaffected by towlower.
    character = static_cast<wchar_t>(towlower(character));
  }

  const int needed = WideCharToMultiByte(CP_UTF8, 0, &character, 1, nullptr, 0, nullptr, nullptr);
  if (needed <= 0) {
    return std::nullopt;
  }
  std::string utf8(static_cast<size_t>(needed), '\0');
  WideCharToMultiByte(CP_UTF8, 0, &character, 1, utf8.data(), needed, nullptr, nullptr);
  return KeyCombination{utf8, modifiers};
}

} // namespace basalt
