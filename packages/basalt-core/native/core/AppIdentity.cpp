#include "AppIdentity.h"

#include <folly/json.h>

#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>

namespace basalt {

namespace {

AppIdentity &storage() {
  static AppIdentity identity;
  return identity;
}

std::once_flag &readOnce() {
  static std::once_flag flag;
  return flag;
}

AppIdentity readFrom(const std::string &bundlePath) {
  AppIdentity identity;

  std::error_code error;
  const std::filesystem::path bundle(bundlePath);
  const std::filesystem::path file =
      (bundle.has_parent_path() ? bundle.parent_path() : std::filesystem::current_path(error)) /
      "app.identity.json";

  std::ifstream stream(file);
  if (!stream) {
    return identity;
  }
  std::stringstream contents;
  contents << stream.rdbuf();

  try {
    const folly::dynamic parsed = folly::parseJson(contents.str());
    if (!parsed.isObject()) {
      return identity;
    }
    if (const auto *name = parsed.get_ptr("name"); name != nullptr && name->isString()) {
      identity.name = name->asString();
    }
    if (const auto *id = parsed.get_ptr("identifier"); id != nullptr && id->isString()) {
      identity.identifier = id->asString();
    }
    if (const auto *version = parsed.get_ptr("version"); version != nullptr && version->isString()) {
      identity.version = version->asString();
    }
    if (const auto *schemes = parsed.get_ptr("schemes"); schemes != nullptr && schemes->isArray()) {
      for (const auto &scheme : *schemes) {
        if (scheme.isString()) {
          identity.schemes.push_back(scheme.asString());
        }
      }
    }
  } catch (const std::exception &) {
    // A malformed file is the same as none. It is written by this project's own
    // CLI, so a parse failure means somebody edited it by hand -- and refusing
    // to start over it would be a worse answer than starting without an icon.
    return AppIdentity{};
  }
  return identity;
}

} // namespace

AppIdentity readAppIdentity(const std::string &bundlePath) {
  return readFrom(bundlePath);
}

const AppIdentity &appIdentity(const std::string &bundlePath) {
  std::call_once(readOnce(), [&bundlePath] { storage() = readFrom(bundlePath); });
  return storage();
}

const AppIdentity &appIdentity() {
  return storage();
}

} // namespace basalt
