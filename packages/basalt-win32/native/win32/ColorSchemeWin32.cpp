// Windows' half of core/ColorScheme.h -- light or dark, as the system has it.
//
// The answer lives in the registry rather than in an API: Windows exposes the
// app theme as `AppsUseLightTheme` under the Personalize key, and there is no
// Win32 function that returns it. WinRT has UISettings, which would be a
// heavier dependency for one boolean and is unavailable in a plain Win32
// process without an apartment. The registry value is what every desktop
// framework reads, and it is what Explorer writes when the setting changes.
//
// Note the sense: the value is "apps use *light* theme", so 1 is light and 0 is
// dark, and an absent value means light -- which is the pre-2018 behaviour and
// still the right default.

#include "ColorScheme.h"

#include <windows.h>

namespace basalt {
namespace {

constexpr const wchar_t *kPersonalizeKey =
    L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
constexpr const wchar_t *kAppsUseLightTheme = L"AppsUseLightTheme";

} // namespace

ColorScheme systemColorScheme() {
  DWORD value = 1;
  DWORD size = sizeof(value);
  const LSTATUS status = RegGetValueW(HKEY_CURRENT_USER,
                                      kPersonalizeKey,
                                      kAppsUseLightTheme,
                                      RRF_RT_REG_DWORD,
                                      nullptr,
                                      &value,
                                      &size);
  if (status != ERROR_SUCCESS) {
    // No value at all: a machine that predates the setting, or a policy that
    // removed it. Light is what those show.
    return ColorScheme::Light;
  }
  return value == 0 ? ColorScheme::Dark : ColorScheme::Light;
}

void startObservingColorScheme() {
  // Not implemented, and deliberately not faked. Watching this properly means
  // RegNotifyChangeKeyValue on a worker thread, or handling WM_SETTINGCHANGE
  // with lParam "ImmersiveColorSet" on the host's window -- and the second is
  // the better answer, because it arrives on the thread that would have to
  // repaint. It belongs with the host for that reason.
  //
  // The other two desktops subscribe and, per docs/BACKLOG.md, have never been
  // watched firing. So nothing is lost here that is proven to work there.
}

} // namespace basalt
