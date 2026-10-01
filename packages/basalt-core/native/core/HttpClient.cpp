// The http seam for the Linux platform, backed by libcurl.
//
// `getHttpClientFactory()` is declared by ReactCxxPlatform and defined nowhere
// in it, exactly like `getDefaultComponentRegistryFactory()`: every host writes
// its own. `ReactHost` refuses to construct without one, so this file is not
// optional even for a host that never makes a request.
//
// Its counterpart, `getWebSocketClientFactory()`, is *not* here. React Native
// already ships a working boost::beast client at
// ReactCxxPlatform/react/http/platform/cxx/WebSocketClient.cpp that its own
// CMakeLists never compiles; this project builds it as `rn_websocket` rather
// than writing a second one. See cmake/ReactNativeCore.cmake.
//
// Two callers matter:
//
//   - `DevServerHelper::downloadBundleResourceSync` fetches the Metro bundle
//     and blocks on a std::future that only `onBody` completes. A client that
//     drops its callbacks does not fail there, it hangs, so every exit path
//     below has to end in `onResponseComplete`.
//   - `NetworkingModule` backs fetch/XHR in JS.
//
// Threading: one thread per request. Callbacks run on that thread, never on the
// GTK main thread or the JS thread. Both callers above expect that -- the dev
// server hands the result to a promise, and NetworkingModule bounces through
// its CallInvoker.

#include "BlobRegistry.h"
#include "DevBundle.h"

#include <react/http/IHttpClient.h>

#include <curl/curl.h>
#include <folly/io/IOBuf.h>
#include <glog/logging.h>

#include <algorithm>
#include <atomic>
// For the fixed-width types in IHttpClient's signatures. Reached through
// DevBundle.h today, which is not a dependency worth relying on.
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace facebook::react {

namespace {

void ensureCurlInitialised() {
  static std::once_flag once;
  std::call_once(once, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// Shared between the caller and the request thread. `cancelled` is the only
// thing the caller touches after handing the request off.
struct RequestState {
  std::atomic<bool> cancelled{false};
};

class CurlRequestToken final : public http::IRequestToken {
 public:
  explicit CurlRequestToken(std::shared_ptr<RequestState> state) : state_(std::move(state)) {}

  void cancel() noexcept override {
    state_->cancelled = true;
  }

 private:
  std::shared_ptr<RequestState> state_;
};

size_t appendToString(char *data, size_t size, size_t count, void *userData) {
  auto *out = static_cast<std::string *>(userData);
  out->append(data, size * count);
  return size * count;
}

size_t collectHeader(char *data, size_t size, size_t count, void *userData) {
  auto *headers = static_cast<http::Headers *>(userData);
  const std::string line(data, size * count);

  // curl hands over the status line and the blank terminator too; neither is a
  // header, and only lines with a colon are.
  const auto colon = line.find(':');
  if (colon != std::string::npos) {
    std::string name = line.substr(0, colon);
    std::string value = line.substr(colon + 1);
    const auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    headers->emplace_back(std::move(name), std::move(value));
  }
  return size * count;
}

int reportProgress(void *userData,
                   curl_off_t downloadTotal,
                   curl_off_t downloadNow,
                   curl_off_t uploadTotal,
                   curl_off_t uploadNow) {
  auto *state = static_cast<RequestState *>(userData);
  (void)downloadTotal;
  (void)downloadNow;
  (void)uploadTotal;
  (void)uploadNow;
  // A non-zero return aborts the transfer, which surfaces as CURLE_ABORTED_BY_CALLBACK.
  return state->cancelled ? 1 : 0;
}

// Runs on the request thread. Always ends in onResponseComplete.
void performRequest(http::NetworkCallbacks callbacks,
                    std::string method,
                    std::string url,
                    http::Headers requestHeaders,
                    std::string requestBody,
                    bool hasBody,
                    uint32_t timeoutSeconds,
                    std::shared_ptr<RequestState> state) {
  CURL *handle = curl_easy_init();
  if (handle == nullptr) {
    if (callbacks.onResponseComplete) {
      callbacks.onResponseComplete("could not initialise libcurl", false);
    }
    return;
  }

  std::string responseBody;
  http::Headers responseHeaders;

  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
  curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendToString);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &responseBody);
  curl_easy_setopt(handle, CURLOPT_HEADERFUNCTION, collectHeader);
  curl_easy_setopt(handle, CURLOPT_HEADERDATA, &responseHeaders);
  curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION, reportProgress);
  curl_easy_setopt(handle, CURLOPT_XFERINFODATA, state.get());
  // Without this, libcurl installs a SIGALRM-based resolver timeout that is not
  // safe to use off the main thread.
  curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);

  if (timeoutSeconds > 0) {
    curl_easy_setopt(handle, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds));
  }

  if (hasBody) {
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, requestBody.c_str());
    curl_easy_setopt(handle, CURLOPT_POSTFIELDSIZE, static_cast<long>(requestBody.size()));
  }

  curl_slist *headerList = nullptr;
  for (const auto &[name, value] : requestHeaders) {
    headerList = curl_slist_append(headerList, (name + ": " + value).c_str());
  }
  if (headerList != nullptr) {
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headerList);
  }

  const CURLcode result = curl_easy_perform(handle);

  long statusCode = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &statusCode);

  // A failed bundle fetch is the one case where the body must not be delivered.
  // `DevServerHelper` hands whatever arrives to the JS engine without reading
  // the status, so Metro's 500 -- which is how it reports every error in your
  // app -- gets compiled as JavaScript. Reporting it as an error instead means
  // `ReactHost` never sees a script, and core/DevBundle.h keeps the message
  // that was in the body. Only the bundle request: an app's own fetch() of a
  // 404 or a 500 needs its body, which is what the status code is for.
  const bool bundleFailed = result == CURLE_OK && (statusCode < 200 || statusCode >= 300) &&
      basalt::isDevBundleRequest(method, url);

  if (bundleFailed) {
    basalt::recordDevBundleError(statusCode, url, responseBody);
    if (callbacks.onResponseComplete) {
      callbacks.onResponseComplete("Metro answered " + std::to_string(statusCode), false);
    }
  } else if (result == CURLE_OK) {
    // The other half of the same question: a bundle that *did* come from Metro,
    // so the host can say so rather than naming the file it would have used.
    if (statusCode >= 200 && statusCode < 300 && basalt::isDevBundleRequest(method, url)) {
      basalt::recordDevBundleFetched(url);
    }
    if (callbacks.onResponse) {
      callbacks.onResponse(static_cast<uint16_t>(statusCode), responseHeaders);
    }
    if (callbacks.onBody) {
      callbacks.onBody(folly::IOBuf::copyBuffer(responseBody));
    }
    if (callbacks.onResponseComplete) {
      callbacks.onResponseComplete("", false);
    }
  } else {
    const bool timedOut = result == CURLE_OPERATION_TIMEDOUT;
    std::string error = curl_easy_strerror(result);
    if (result == CURLE_ABORTED_BY_CALLBACK) {
      error = "request cancelled";
    }
    if (callbacks.onResponseComplete) {
      // A timeout reports through the dedicated flag, and DevServerHelper
      // treats an empty error string with that flag set as "Timeout".
      callbacks.onResponseComplete(timedOut ? "" : error, timedOut);
    }
  }

  if (headerList != nullptr) {
    curl_slist_free_all(headerList);
  }
  curl_easy_cleanup(handle);
}

class CurlHttpClient final : public IHttpClient {
 public:
  std::unique_ptr<http::IRequestToken> sendRequest(
      http::NetworkCallbacks &&callbacks,
      const std::string &method,
      const std::string &url,
      const http::Headers &headers = {},
      const http::Body &body = {},
      uint32_t timeout = 0,
      std::optional<std::string> /*loggingId*/ = std::nullopt) override {
    ensureCurlInitialised();

    // A string body, or a blob's bytes looked up by id.
    //
    // `http::Body::blob` is typed `std::optional<std::string>` upstream, and
    // JavaScript sends `{blobId, offset, size}` -- an object. So a Blob body
    // cannot currently reach here at all: ReactCxxPlatform's bridging tries to
    // read that object as a string before this code is ever called. Handling it
    // anyway, as an id, because that is what the type says it is and because
    // the day upstream fixes the type this becomes correct rather than needing
    // to be written.
    std::string requestBody = body.string.value_or(std::string{});
    bool hasBody = body.string.has_value();

    if (body.blob) {
      if (auto bytes = basalt::blobBytes(*body.blob)) {
        requestBody = std::move(*bytes);
        hasBody = true;
      } else {
        LOG(WARNING) << "http: request body names an unknown blob '" << *body.blob << "' ("
                     << method << " " << url << ")";
      }
    }

    // Form-data and base64 bodies come from JS APIs nothing here can reach yet:
    // there is no file picker and no camera.
    if (body.formData || body.base64) {
      LOG(WARNING) << "http: form-data and base64 request bodies are not supported (" << method
                   << " " << url << ")";
    }

    auto state = std::make_shared<RequestState>();

    std::thread(performRequest,
                std::move(callbacks),
                method,
                url,
                headers,
                std::move(requestBody),
                hasBody,
                timeout,
                state)
        .detach();

    return std::make_unique<CurlRequestToken>(state);
  }
};

} // namespace

HttpClientFactory getHttpClientFactory() {
  return []() { return std::make_unique<CurlHttpClient>(); };
}

} // namespace facebook::react
