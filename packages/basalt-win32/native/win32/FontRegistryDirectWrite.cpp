// Windows' half of core/FontRegistry.h -- the seam phase 17 found by linking.
//
// `expo-font` arrives here: an app loads a .ttf at runtime and then names it in
// a `fontFamily`, so two things have to happen. DirectWrite has to be able to
// find the file, and `resolveFontFamily` has to map the name the app used onto
// whatever the font actually calls itself.
//
// The first is `AddFontResourceExW` with FR_PRIVATE, which adds the file to the
// process's own font set. On Windows 10 1809 and later DirectWrite's system
// font collection picks those up, which is why this is a two-line
// implementation rather than an IDWriteFontSetBuilder and a custom collection.
// That is also the thing about it most worth distrusting -- see the note at the
// bottom.

#include "FontRegistry.h"

#include "Win32Strings.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>

namespace basalt {

using win32::widen;
namespace {

struct Registry {
  std::mutex mutex;
  // The name the app asked for, against the path it gave. Kept because
  // `resolveFontFamily` has to answer for a name DirectWrite may not know by
  // that spelling.
  std::unordered_map<std::string, std::string> fonts;
};

Registry &registry() {
  static Registry instance;
  return instance;
}

// Bumped on every successful registration. The text layer reads it to know its
// cached layouts may be stale: a paragraph measured before a font arrived was
// measured with a fallback, and would keep those metrics forever otherwise.
std::atomic<unsigned long> generation{0};

} // namespace

bool registerFont(const std::string &name, const std::string &path) {
  if (name.empty() || path.empty()) {
    return false;
  }

  const std::wstring widePath = widen(path);
  // FR_PRIVATE: visible to this process only. A font an app loaded is not
  // something to install system-wide, and doing so would need elevation.
  if (AddFontResourceExW(widePath.c_str(), FR_PRIVATE, nullptr) == 0) {
    return false;
  }

  {
    std::lock_guard<std::mutex> lock(registry().mutex);
    registry().fonts[name] = path;
  }
  generation.fetch_add(1, std::memory_order_relaxed);
  return true;
}

std::string resolveFontFamily(const std::string &name) {
  std::lock_guard<std::mutex> lock(registry().mutex);
  const auto it = registry().fonts.find(name);
  if (it == registry().fonts.end()) {
    // Not one of ours: hand the name back and let DirectWrite's own lookup have
    // it. That is what makes "Segoe UI" work without being registered.
    return name;
  }
  // The name the app used is the name it will ask for. Windows matches on the
  // font's *family* name rather than its filename, and those agree for most
  // fonts an app ships; where they do not, this returns the wrong one. Reading
  // the family out of the file with IDWriteFontFile would be exact and is the
  // fix if this ever misses.
  return name;
}

unsigned long fontGeneration() {
  return generation.load(std::memory_order_relaxed);
}

bool isFontRegistered(const std::string &name) {
  std::lock_guard<std::mutex> lock(registry().mutex);
  return registry().fonts.count(name) != 0;
}

std::string registeredFontNamesJoined(char separator) {
  std::lock_guard<std::mutex> lock(registry().mutex);
  std::string joined;
  for (const auto &[name, path] : registry().fonts) {
    (void)path;
    if (!joined.empty()) {
      joined += separator;
    }
    joined += name;
  }
  return joined;
}

// Worth distrusting, and recorded in docs/BACKLOG.md alongside the same doubt
// about macOS: no font has been loaded at runtime and then rendered, on any of
// the three desktops. AddFontResourceEx is documented to make a font available
// to GDI; that DirectWrite's system collection also sees it is true on current
// Windows and is the sort of thing that is true until it is not. The test that
// would settle it is an app calling expo-font and then drawing in that family,
// which needs a host.

} // namespace basalt
