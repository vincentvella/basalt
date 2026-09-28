// What happened when this process asked Metro for its bundle.
//
// React Native's cxx `DevServerHelper` hands the response body straight to the
// JS engine without looking at the status code, and `ReactHost` treats any
// failure as "Metro is not running" and quietly loads the on-disk bundle
// instead. That is right for one of the two ways the fetch can fail and wrong
// for the other:
//
//   - Nothing is listening. curl reports a connection error, no body is
//     produced, and falling back to the prebuilt bundle is exactly what should
//     happen. Verified.
//   - Metro is running and answers 500, which is what it does for any error in
//     your app -- a syntax error, a missing import, a failing transform. The
//     body is Metro's JSON error report, and compiling it as JavaScript
//     produces `Compiling JS failed: 1:8:';' expected`, naming a line in
//     Metro's error rather than a line in your code.
//
// This file separates the two. `core/HttpClient.cpp` recognises the bundle
// request, and on a non-2xx status records the failure here and reports an
// error instead of a body -- so nothing tries to compile it. The host reads
// `devBundleError()` after `loadScript` returns and refuses to start on a
// server error, because the alternative is running yesterday's bundle while the
// developer stares at code that is not what is executing.

#pragma once

// For uint32_t, which nothing else here brings in. It compiled on Windows
// only because windows.h had already been pulled in behind it, and on a Mac for
// the same kind of reason; Linux said so, which is what the Linux job is for.
#include <cstdint>
#include <optional>
#include <string>

namespace basalt {

// Called by the host in dev mode, before the bundle is fetched. Without it
// `isDevBundleRequest` matches nothing and everything below is inert -- which
// is the correct behaviour in release, where there is no dev server.
void setDevServerOrigin(const std::string &host, uint32_t port);

// True for the one request that fetches the bundle: a GET to the dev server's
// own origin for a path ending in `.bundle`. An app's own fetch() cannot match
// unless it deliberately asks Metro for a bundle.
bool isDevBundleRequest(const std::string &method, const std::string &url);

struct DevBundleError {
  long status{0};
  std::string message;
};

// Records a non-2xx response to the bundle request. `body` is Metro's error
// payload; the message is extracted from it if it is the JSON Metro sends.
void recordDevBundleError(long status, const std::string &url, const std::string &body);

// The last recorded failure, if any. Set on the request thread and read on the
// main thread after loadScript has joined it.
std::optional<DevBundleError> devBundleError();

// That the bundle came from Metro, with the URL it came from.
//
// Recorded for one reason: the host's "loaded script" line named the on-disk
// path either way, because that is the argument `loadScript` was given -- and
// `ReactHost` does not say which of the two it used. So a dev run against Metro
// reported the path of a bundle it had not executed, and a stale file on disk
// looked like the thing that was running. That cost an afternoon, twice.
void recordDevBundleFetched(const std::string &url);
std::optional<std::string> devBundleFetched();

} // namespace basalt
