// `Settings`, which on iOS is `NSUserDefaults` and on a desktop is a file.
//
// React Native's `Settings` is four calls -- `get`, `set`, `watchKeys`,
// `clearWatch` -- over one native module, `SettingsManager`, which is a
// `getEnforcing` lookup. Nobody answered it here, so an app that reached for
// the API got React Native's `SettingsFallback`: three `console.warn`s and a
// null. The module was never even reached, for the same reason `Share` was not
// -- `Settings.js` branches on `Platform.OS === 'ios'` and everything else
// takes the fallback -- so this needs the JavaScript override as well; see
// src/overrides/Settings.ts.
//
// ## Why a file rather than each desktop's own settings store
//
// Each of the three has one, and none of them is the same shape as this API.
// macOS has `NSUserDefaults`, which is the thing `Settings` wraps and which a
// host could use directly. GTK's nearest equivalent is `GSettings`, which
// refuses a key that is not in a compiled schema -- and this API's whole
// contract is that an app invents its own keys at runtime, so a schema cannot
// exist. Windows has the registry, which has no JSON and no arrays.
//
// So: a JSON object in a file, identically on all three. One implementation
// with one set of semantics beats one real answer and two approximations that
// differ in what they can store. The cost is that macOS settings are not
// visible to `defaults read`, which is recorded in docs/backlog/modules.md
// along with the call that would change it.
//
// ## What is in the file
//
// A single JSON object, written whole on every change. Not a log and not a
// database: `Settings` is a handful of keys an app remembers between runs --
// a window's last tab, whether the tour was seen -- and an app with more than
// that wants AsyncStorage or SQLite, neither of which this pretends to be.
//
// Written to a sibling and renamed over, so a process that dies mid-write
// leaves the previous file rather than half of a new one. That matters more
// here than it looks: this file is read at startup, and a truncated one would
// make every later run start empty.
//
// ## What is not here
//
// `watchKeys` only ever fires for a change this process did not make. That is
// not a shortcut: iOS sets `_ignoringUpdates` around its own `setValues` for
// exactly the same reason, because `Settings.set` has already updated
// JavaScript's copy by the time the native module is called. Firing for an
// external change means watching the file, which is three different APIs, and
// is recorded rather than written; see docs/backlog/modules.md.

#pragma once

#include <folly/dynamic.h>

#include <filesystem>
#include <string>
#include <vector>

namespace basalt {

// Where the settings live, or an empty path when there is nowhere to put them.
//
// `BASALT_SETTINGS_FILE` wins when it is set, which is how the end-to-end
// scenario points two runs of a host at the same temporary file instead of at
// the person's real one, and how a portable install keeps its settings beside
// itself.
//
// Otherwise it is the per-user configuration directory each desktop names --
// `$XDG_CONFIG_HOME` or `~/.config` on Linux, `~/Library/Application Support`
// on macOS, `%APPDATA%` on Windows -- plus the application's identifier, plus
// `settings.json`. The identifier is what `app.identity.json` says, so two
// applications on one machine do not share a file; an app built by hand has no
// identity and lands under `basalt`, which is the honest answer for a binary
// that has not said who it is.
//
// Empty when even the home directory is unknown, which is a container with no
// environment rather than a mistake. A store with no path keeps its values in
// memory for the life of the process and persists nothing.
std::filesystem::path settingsFilePath();

// One spelling, in one place, for the same reason kTestQuitFileVar is.
inline constexpr const char *kSettingsFileVar = "BASALT_SETTINGS_FILE";

// The key-value store behind `SettingsManager`.
//
// Loaded in the constructor and saved on every change. Not thread-safe and not
// required to be: every caller is a TurboModule method, which runs on the
// JavaScript thread.
class SettingsStore {
 public:
  // Reads `file` if it is there. A missing file is the normal first run; a file
  // that is not a JSON object is logged and treated as empty, because an app
  // that refuses to start over a settings file somebody hand-edited would be
  // worse than one that starts with no settings.
  explicit SettingsStore(std::filesystem::path file);

  // Every setting, as the object `getConstants().settings` hands to JavaScript.
  const folly::dynamic &values() const {
    return values_;
  }

  // Merges `values` in, one key deep, and answers the keys whose value actually
  // changed. A key whose new value equals the old one is not in that list,
  // which is what decides whether anything needs writing.
  //
  // A null value deletes the key rather than storing a null. That is iOS's
  // behaviour -- `RCTSettingsManager` calls `removeObjectForKey:` when
  // `RCTConvert NSPropertyList:` answers nil -- and an app that sets a key to
  // null means to forget it.
  std::vector<std::string> merge(const folly::dynamic &values);

  // Removes `keys`, answering the ones that were actually there.
  std::vector<std::string> erase(const std::vector<std::string> &keys);

  // Where it is kept, for the host that wants to say so in a log.
  const std::filesystem::path &file() const {
    return file_;
  }

 private:
  // Writes the whole object, through a sibling and a rename. Failure is logged
  // and not reported: there is nothing an app could do about a read-only home
  // directory, and losing a setting is not worth refusing to run over.
  void save() const;

  std::filesystem::path file_;
  folly::dynamic values_ = folly::dynamic::object();
};

} // namespace basalt
