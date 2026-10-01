#include "Win32Packaging.h"

#include "AppIdentity.h"
#include "Win32Strings.h"

#include <windows.h>

#include <knownfolders.h>
#include <objbase.h>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <shlobj.h>
#include <shobjidl.h>

#include <string>

namespace basalt::win32 {

namespace {

std::wstring &applied() {
  static std::wstring id;
  return id;
}

// Where a per-user Start Menu shortcut goes. Per-user rather than per-machine
// because this is not an installer: writing into the all-users Start Menu needs
// administrator rights, and an app being run from a build directory has no
// business asking for them.
std::wstring startMenuShortcutPath(const std::wstring &name) {
  PWSTR folder = nullptr;
  if (FAILED(SHGetKnownFolderPath(FOLDERID_Programs, 0, nullptr, &folder))) {
    return {};
  }
  std::wstring path(folder);
  CoTaskMemFree(folder);
  path += L"\\";
  path += name;
  path += L".lnk";
  return path;
}

// The executable this process was started from, which is what the shortcut has
// to point at.
std::wstring executablePath() {
  std::wstring path(MAX_PATH, L'\0');
  while (true) {
    const DWORD written = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (written == 0) {
      return {};
    }
    if (written < path.size()) {
      path.resize(written);
      return path;
    }
    // Truncated. The only way to know is that it filled the buffer, so grow and
    // ask again rather than shipping a path that is quietly half a path.
    path.resize(path.size() * 2);
  }
}

// Writes the shortcut, with the AppUserModelID on it. Returns false on any
// failure, all of which are survivable: what is lost is notifications, not the
// app.
bool writeShortcut(const std::wstring &shortcutPath,
                   const std::wstring &target,
                   const std::wstring &id) {
  IShellLinkW *link = nullptr;
  if (FAILED(CoCreateInstance(
          CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void **)&link))) {
    return false;
  }

  bool ok = SUCCEEDED(link->SetPath(target.c_str()));
  if (ok) {
    // The working directory matters: the host resolves its bundle relative to
    // wherever it was started, so a shortcut with the wrong one launches an app
    // that cannot find its JavaScript.
    const size_t slash = target.find_last_of(L'\\');
    if (slash != std::wstring::npos) {
      link->SetWorkingDirectory(target.substr(0, slash).c_str());
    }
  }

  // The property that makes the whole thing work. A shortcut without it is
  // just a shortcut, and the shell goes on refusing to show notifications for
  // this id.
  if (ok) {
    IPropertyStore *properties = nullptr;
    ok = SUCCEEDED(link->QueryInterface(IID_IPropertyStore, (void **)&properties));
    if (ok) {
      PROPVARIANT value;
      ok = SUCCEEDED(InitPropVariantFromString(id.c_str(), &value));
      if (ok) {
        ok = SUCCEEDED(properties->SetValue(PKEY_AppUserModel_ID, value));
        if (ok) {
          ok = SUCCEEDED(properties->Commit());
        }
        PropVariantClear(&value);
      }
      properties->Release();
    }
  }

  if (ok) {
    IPersistFile *file = nullptr;
    ok = SUCCEEDED(link->QueryInterface(IID_IPersistFile, (void **)&file));
    if (ok) {
      ok = SUCCEEDED(file->Save(shortcutPath.c_str(), TRUE));
      file->Release();
    }
  }

  link->Release();
  return ok;
}

} // namespace

void applyPackaging(const AppIdentity &identity) {
  if (identity.empty()) {
    return;
  }

  const std::wstring id = widen(identity.identifier);
  // Before any window. The shell reads this when a window is first shown and
  // does not notice a later change, which is why this is called from the top of
  // the host rather than from wherever notifications are first used.
  if (FAILED(SetCurrentProcessExplicitAppUserModelID(id.c_str()))) {
    return;
  }
  applied() = id;

  const std::wstring name = widen(identity.name.empty() ? identity.identifier : identity.name);
  const std::wstring shortcut = startMenuShortcutPath(name);
  if (shortcut.empty()) {
    return;
  }
  // Left alone once written. This is a real file in the person's Start Menu,
  // and a development session that rewrote it on every run would churn their
  // menu for nothing.
  if (GetFileAttributesW(shortcut.c_str()) != INVALID_FILE_ATTRIBUTES) {
    return;
  }
  const std::wstring target = executablePath();
  if (target.empty()) {
    return;
  }
  writeShortcut(shortcut, target, id);
}

bool hasAppUserModelId() {
  return !applied().empty();
}

const wchar_t *appUserModelId() {
  return applied().c_str();
}

} // namespace basalt::win32
