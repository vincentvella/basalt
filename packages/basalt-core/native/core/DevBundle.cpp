#include "DevBundle.h"

#include <folly/json.h>
#include <glog/logging.h>

#include <mutex>

namespace basalt {

namespace {

std::mutex &stateMutex() {
  static std::mutex mutex;
  return mutex;
}

std::string &devOrigin() {
  static std::string origin;
  return origin;
}

std::optional<DevBundleError> &lastError() {
  static std::optional<DevBundleError> error;
  return error;
}

// Metro answers an error with JSON: `type`, `message`, and usually a code frame
// already formatted into `message` with ANSI colour. Anything else -- a proxy's
// HTML, a plain string -- comes back as itself.
std::string messageFrom(const std::string &body) {
  try {
    const folly::dynamic parsed = folly::parseJson(body);
    if (parsed.isObject()) {
      if (const auto *message = parsed.get_ptr("message"); message != nullptr && message->isString()) {
        return message->asString();
      }
    }
  } catch (const std::exception &) {
    // Not JSON. Fall through to the body itself.
  }

  constexpr size_t kMaxRawBody = 4000;
  return body.size() > kMaxRawBody ? body.substr(0, kMaxRawBody) + "\n[...truncated]" : body;
}

} // namespace

void setDevServerOrigin(const std::string &host, uint32_t port) {
  const std::lock_guard<std::mutex> lock(stateMutex());
  devOrigin() = "http://" + host + ":" + std::to_string(port) + "/";
}

bool isDevBundleRequest(const std::string &method, const std::string &url) {
  if (method != "GET") {
    return false;
  }

  std::string origin;
  {
    const std::lock_guard<std::mutex> lock(stateMutex());
    origin = devOrigin();
  }
  if (origin.empty() || url.compare(0, origin.size(), origin) != 0) {
    return false;
  }

  // `/index.bundle?platform=...`, or `/index.bundle` with no query at all.
  const auto query = url.find('?');
  const std::string path = query == std::string::npos ? url : url.substr(0, query);
  constexpr std::string_view kSuffix = ".bundle";
  return path.size() >= kSuffix.size() &&
      path.compare(path.size() - kSuffix.size(), kSuffix.size(), kSuffix) == 0;
}

void recordDevBundleError(long status, const std::string &url, const std::string &body) {
  const std::string message = messageFrom(body);

  // Logged here rather than left to the host, because this is the moment the
  // developer's actual error is in hand and there is nowhere else it survives.
  LOG(ERROR) << "Metro answered " << status << " for " << url << "\n" << message;

  const std::lock_guard<std::mutex> lock(stateMutex());
  lastError() = DevBundleError{.status = status, .message = message};
}

namespace {

std::optional<std::string> &fetched() {
  static std::optional<std::string> url;
  return url;
}

} // namespace

void recordDevBundleFetched(const std::string &url) {
  fetched() = url;
}

std::optional<std::string> devBundleFetched() {
  return fetched();
}

std::optional<DevBundleError> devBundleError() {
  const std::lock_guard<std::mutex> lock(stateMutex());
  return lastError();
}

} // namespace basalt
