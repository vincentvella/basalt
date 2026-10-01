// What the app calls itself, as the desktop needs to know it.
//
// A host is a generic binary: it is handed a bundle and a module name and has
// no idea what application it is. That is fine for drawing and not fine for
// anything the operating system keys on identity -- a notification's name and
// icon, a taskbar group, a registered URL scheme.
//
// `cli/packageApp.js` writes the answer beside the bundle as `app.identity.json`
// and this reads it. Beside the bundle rather than beside the executable
// because the executable is shared by every app on the machine, and beside it
// rather than passed as an argument because a shortcut the person clicks in a
// month has no environment to carry one.
//
// Everything here degrades rather than fails: an app built by hand, with no
// identity file, gets an empty identity, and each caller decides what that
// means. Windows treats it as "no AppUserModelID, so no toasts", which is the
// same answer it gave before any of this existed.

#pragma once

#include <string>
#include <vector>

namespace basalt {

struct AppIdentity {
  // What a person sees: "The Demo".
  std::string name;
  // Reverse-DNS, and on Windows also the AppUserModelID: "com.example.demo".
  std::string identifier;
  // URL schemes the app claims, without the "://".
  std::vector<std::string> schemes;
  std::string version;

  bool empty() const {
    return identifier.empty();
  }
};

// Reads `app.identity.json` from beside `bundlePath`, with no caching. Returns
// an empty identity when there is none, when it cannot be parsed, or when it
// says nothing useful -- none of which is an error.
//
// Separate from the cached reader below so that it can be tested: the cache is
// process-wide and answers once, which a test cannot work with.
AppIdentity readAppIdentity(const std::string &bundlePath);

// Reads `app.identity.json` from beside `bundlePath`. Returns an empty identity
// when there is none, which is not an error.
//
// Read once and cached: it is consulted from startup and from the notification
// path, and it cannot change under a running process.
const AppIdentity &appIdentity(const std::string &bundlePath);

// The identity read earlier, or an empty one when nothing has read it yet.
// For the callers that have no bundle path in reach.
const AppIdentity &appIdentity();

} // namespace basalt
