#include "AppPaths.h"

#include "AppIdentity.h"

#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <utility>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace basalt {

namespace {

// The running executable, by each platform's own way of asking. There is no
// portable one: argv[0] is whatever the caller passed and may be a bare name.
std::string executablePath() {
#if defined(__APPLE__)
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  // Sized to include the terminator, so the string has one inside it.
  return buffer.c_str();
#elif defined(_WIN32)
  std::wstring buffer(MAX_PATH, L'\0');
  DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  while (length == buffer.size()) {
    // The path was longer than the buffer, which GetModuleFileNameW reports by
    // filling it exactly; there is no other way to ask how long it is.
    buffer.resize(buffer.size() * 2);
    length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
  }
  if (length == 0) {
    return {};
  }
  buffer.resize(length);
  const int bytes = WideCharToMultiByte(
      CP_UTF8, 0, buffer.c_str(), static_cast<int>(buffer.size()), nullptr, 0, nullptr, nullptr);
  if (bytes <= 0) {
    return {};
  }
  std::string utf8(static_cast<size_t>(bytes), '\0');
  WideCharToMultiByte(
      CP_UTF8, 0, buffer.c_str(), static_cast<int>(buffer.size()), utf8.data(), bytes, nullptr,
      nullptr);
  return utf8;
#else
  std::error_code code;
  const std::filesystem::path resolved = std::filesystem::read_symlink("/proc/self/exe", code);
  return code ? std::string{} : resolved.string();
#endif
}

} // namespace

std::string applicationResourcePath() {
  const std::string executable = executablePath();
  if (executable.empty()) {
    return {};
  }

  std::error_code ignored;
  const std::filesystem::path directory =
      std::filesystem::weakly_canonical(std::filesystem::path(executable).parent_path(), ignored);

  // `Foo.app/Contents/MacOS/basalt_appkit` -> `Foo.app/Contents/Resources`,
  // which is where cli/packageApp.ts puts an app's files and where
  // NSBundle.mainBundle.resourcePath would point. Recognised by the layout
  // rather than by the platform, because a macOS host run straight out of a
  // build directory is not in a bundle and should answer that directory.
  if (directory.filename() == "MacOS" && directory.parent_path().filename() == "Contents") {
    return (directory.parent_path() / "Resources").string();
  }
  return directory.string();
}

std::string packagedBundlePath() {
  const std::string resources = applicationResourcePath();
  if (resources.empty()) {
    return {};
  }
  std::error_code ignored;
  const std::filesystem::path bundle = std::filesystem::path(resources) / "main.jsbundle.js";
  return std::filesystem::is_regular_file(bundle, ignored) ? bundle.string() : std::string();
}

std::string applicationDataPath() {
  // The identifier the rest of the platform already uses: the Info.plist's
  // CFBundleIdentifier, the .desktop file's name, the AppUserModelID. An app
  // that declared none has the invented `com.basalt.<slug>` by the time this
  // is asked, so there is always something to name a directory after.
  // `cli/packageApp.ts` invents `com.basalt.<slug>` for an app that declared
  // none, and writes it into app.identity.json; a development run has no such
  // file and lands here with nothing.
  std::string identifier = appIdentity().identifier;
  if (identifier.empty()) {
    identifier = "basalt.development";
  }

  std::filesystem::path base;
#if defined(_WIN32)
  // LOCALAPPDATA rather than APPDATA: this is a cache of the app's own making
  // rather than something to roam between machines, and a roaming profile is
  // a surprising place to put a database that may grow.
  if (const char *local = std::getenv("LOCALAPPDATA"); local != nullptr && *local != '\0') {
    base = local;
  }
#elif defined(__APPLE__)
  if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    base = std::filesystem::path(home) / "Library" / "Application Support";
  }
#else
  // The XDG basedir spec: the variable if it is set to an absolute path, and
  // the default it documents otherwise. A relative value is to be ignored
  // rather than resolved, which the spec says in as many words.
  if (const char *xdg = std::getenv("XDG_DATA_HOME");
      xdg != nullptr && *xdg == '/') {
    base = xdg;
  } else if (const char *home = std::getenv("HOME"); home != nullptr && *home != '\0') {
    base = std::filesystem::path(home) / ".local" / "share";
  }
#endif
  if (base.empty()) {
    return {};
  }

  const std::filesystem::path directory = base / identifier;
  std::error_code code;
  std::filesystem::create_directories(directory, code);
  // Reported as "nowhere to write" rather than thrown: a store that cannot be
  // created is a feature that does not work, and the module above turns that
  // into a rejected promise with a reason. Throwing here would take the
  // application down on a read-only home directory.
  if (code && !std::filesystem::is_directory(directory, code)) {
    return {};
  }
  return directory.string();
}

DesktopAppModule::DesktopAppModule(std::shared_ptr<facebook::react::CallInvoker> jsInvoker)
    : TurboModule(kModuleName, std::move(jsInvoker)) {
  methodMap_["getResourcePath"] = MethodMetadata{0, getResourcePath};
}

facebook::jsi::Value DesktopAppModule::getResourcePath(
    facebook::jsi::Runtime &runtime,
    facebook::react::TurboModule & /*module*/,
    const facebook::jsi::Value * /*args*/,
    size_t /*count*/) {
  return facebook::jsi::String::createFromUtf8(runtime, applicationResourcePath());
}

} // namespace basalt
