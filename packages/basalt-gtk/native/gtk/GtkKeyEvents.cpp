#include "GtkKeyEvents.h"

#include <glib.h>

#include <string>

namespace basalt {
namespace {

const char *specialNameFor(guint keyval) {
  switch (keyval) {
  case GDK_KEY_Up:
  case GDK_KEY_KP_Up:
    return kKeyArrowUp;
  case GDK_KEY_Down:
  case GDK_KEY_KP_Down:
    return kKeyArrowDown;
  case GDK_KEY_Left:
  case GDK_KEY_KP_Left:
    return kKeyArrowLeft;
  case GDK_KEY_Right:
  case GDK_KEY_KP_Right:
    return kKeyArrowRight;
  case GDK_KEY_Page_Up:
  case GDK_KEY_KP_Page_Up:
    return kKeyPageUp;
  case GDK_KEY_Page_Down:
  case GDK_KEY_KP_Page_Down:
    return kKeyPageDown;
  case GDK_KEY_Home:
  case GDK_KEY_KP_Home:
    return kKeyHome;
  case GDK_KEY_End:
  case GDK_KEY_KP_End:
    return kKeyEnd;
  case GDK_KEY_Delete:
  case GDK_KEY_KP_Delete:
    return kKeyDelete;
  case GDK_KEY_BackSpace:
    return kKeyBackspace;
  case GDK_KEY_Return:
  case GDK_KEY_KP_Enter:
    return kKeyEnter;
  case GDK_KEY_Tab:
  // Shift-Tab is its own keyval here, as back-tab is its own character on
  // AppKit. Reported as Tab with shift set, for the same reason: that is what a
  // browser reports and what an app will have bound.
  case GDK_KEY_ISO_Left_Tab:
    return kKeyTab;
  case GDK_KEY_Escape:
    return kKeyEscape;
  default:
    return nullptr;
  }
}

} // namespace

std::optional<KeyCombination> keyCombinationFrom(guint keyval, GdkModifierType state) {
  KeyModifiers modifiers;
  modifiers.alt = (state & GDK_ALT_MASK) != 0;
  modifiers.ctrl = (state & GDK_CONTROL_MASK) != 0;
  // Super is `meta`. W3C names the Windows and Command keys Meta, and this is the
  // key in that position on a Linux keyboard -- so an app that binds metaKey gets
  // the same physical key on all three desktops. GDK_META_MASK is a different
  // key again on X11, historically the one next to Alt, and almost nothing has
  // one; mapping it here would give an app a modifier it cannot press.
  modifiers.meta = (state & GDK_SUPER_MASK) != 0;
  modifiers.shift = (state & GDK_SHIFT_MASK) != 0;

  if (const char *special = specialNameFor(keyval)) {
    return KeyCombination{std::string(special), modifiers};
  }

  // A keyval already accounts for Shift -- `A` and `a` are different keyvals --
  // so case follows the character here without doing anything, which is what a
  // browser reports and what AppKit needs `characters` for.
  const guint32 codepoint = gdk_keyval_to_unicode(keyval);
  if (codepoint == 0) {
    // A bare modifier, or a keyval with no character and no name of ours.
    return std::nullopt;
  }
  char utf8[8] = {};
  const gint length = g_unichar_to_utf8(static_cast<gunichar>(codepoint), utf8);
  if (length <= 0) {
    return std::nullopt;
  }
  return KeyCombination{std::string(utf8, static_cast<size_t>(length)), modifiers};
}

} // namespace basalt
