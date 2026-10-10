#include "SettingsStore.h"

#include "AppIdentity.h"

#include <folly/json.h>

#include <glog/logging.h>

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <system_error>
#include <utility>

namespace basalt {

namespace {

std::string environmentValue(const char *name) {
  const char *value = std::getenv(name);
  return value == nullptr ? std::string{} : std::string(value);
}

// The per-user configuration directory, by each desktop's own rule.
//
// Read from the environment rather than asked of an API on purpose: the
// authoritative calls are `g_get_user_config_dir`, `-[NSFileManager
// URLsForDirectory:inDomains:]` and `SHGetKnownFolderPath`, which are three
// libraries this file does not link and which all three answer from the same
// variables anyway. A host that wants the real call can pass a path through
// `BASALT_SETTINGS_FILE`.
std::filesystem::path userConfigDirectory() {
#if defined(_WIN32)
  const std::string appData = environmentValue("APPDATA");
  if (!appData.empty()) {
    return std::filesystem::path(appData);
  }
  const std::string profile = environmentValue("USERPROFILE");
  if (profile.empty()) {
    return {};
  }
  return std::filesystem::path(profile) / "AppData" / "Roaming";
#else
  const std::string home = environmentValue("HOME");
#if defined(__APPLE__)
  // Where a macOS application's own files go. `~/Library/Preferences` is where
  // `NSUserDefaults` writes, and writing a file of our own into it would put
  // something that is not a plist where `defaults` expects one.
  if (home.empty()) {
    return {};
  }
  return std::filesystem::path(home) / "Library" / "Application Support";
#else
  const std::string configHome = environmentValue("XDG_CONFIG_HOME");
  if (!configHome.empty()) {
    return std::filesystem::path(configHome);
  }
  if (home.empty()) {
    return {};
  }
  return std::filesystem::path(home) / ".config";
#endif
#endif
}

} // namespace

std::filesystem::path settingsFilePath() {
  const std::string chosen = environmentValue(kSettingsFileVar);
  if (!chosen.empty()) {
    return std::filesystem::path(chosen);
  }

  const std::filesystem::path directory = userConfigDirectory();
  if (directory.empty()) {
    return {};
  }

  const AppIdentity &identity = appIdentity();
  return directory / (identity.identifier.empty() ? std::string("basalt") : identity.identifier) /
      "settings.json";
}

SettingsStore::SettingsStore(std::filesystem::path file) : file_(std::move(file)) {
  if (file_.empty()) {
    return;
  }

  std::ifstream stream(file_);
  if (!stream) {
    // The normal first run. Not a warning: an application that has never
    // written a setting has no settings file, and saying so at every startup
    // would train people to ignore the log.
    return;
  }
  std::stringstream contents;
  contents << stream.rdbuf();

  try {
    folly::dynamic parsed = folly::parseJson(contents.str());
    if (parsed.isObject()) {
      values_ = std::move(parsed);
      return;
    }
    LOG(WARNING) << file_.string() << " holds " << parsed.typeName()
                 << " rather than an object; starting with no settings";
  } catch (const std::exception &error) {
    LOG(WARNING) << "could not read " << file_.string() << ": " << error.what();
  }
}

std::vector<std::string> SettingsStore::merge(const folly::dynamic &values) {
  std::vector<std::string> changed;
  if (!values.isObject()) {
    return changed;
  }

  for (const auto &pair : values.items()) {
    if (!pair.first.isString()) {
      // JavaScript object keys are strings, so this is unreachable from the
      // module and reachable from a caller in C++.
      continue;
    }
    const std::string key = pair.first.asString();

    if (pair.second.isNull()) {
      if (values_.count(key) > 0) {
        values_.erase(key);
        changed.push_back(key);
      }
      continue;
    }

    if (values_.count(key) > 0 && values_[key] == pair.second) {
      continue;
    }
    values_[key] = pair.second;
    changed.push_back(key);
  }

  if (!changed.empty()) {
    save();
  }
  return changed;
}

std::vector<std::string> SettingsStore::erase(const std::vector<std::string> &keys) {
  std::vector<std::string> removed;
  for (const std::string &key : keys) {
    if (values_.count(key) == 0) {
      continue;
    }
    values_.erase(key);
    removed.push_back(key);
  }
  if (!removed.empty()) {
    save();
  }
  return removed;
}

void SettingsStore::save() const {
  if (file_.empty()) {
    return;
  }

  std::error_code error;
  if (file_.has_parent_path()) {
    std::filesystem::create_directories(file_.parent_path(), error);
    if (error) {
      LOG(WARNING) << "could not create " << file_.parent_path().string() << ": "
                   << error.message();
      return;
    }
  }

  // A sibling rather than the temporary directory, because the rename below has
  // to stay on one filesystem to be atomic.
  const std::filesystem::path pending(file_.string() + ".new");
  {
    std::ofstream stream(pending, std::ios::binary | std::ios::trunc);
    stream << folly::toPrettyJson(values_) << "\n";
    if (!stream) {
      LOG(WARNING) << "could not write " << pending.string();
      std::error_code ignored;
      std::filesystem::remove(pending, ignored);
      return;
    }
  }

  std::filesystem::rename(pending, file_, error);
  if (error) {
    LOG(WARNING) << "could not replace " << file_.string() << ": " << error.message();
    std::error_code ignored;
    std::filesystem::remove(pending, ignored);
  }
}

} // namespace basalt
