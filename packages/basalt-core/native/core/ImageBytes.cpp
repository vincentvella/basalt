#include "ImageBytes.h"

#include <curl/curl.h>

#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <sstream>

namespace basalt {

namespace {

std::once_flag curlInitFlag;

size_t appendBytes(char *data, size_t size, size_t count, void *userData) {
  auto *out = static_cast<std::string *>(userData);
  out->append(data, size * count);
  return size * count;
}

bool startsWith(const std::string &value, const char *prefix) {
  return value.rfind(prefix, 0) == 0;
}

// Standard base64, ignoring whitespace and tolerating missing padding. Written
// out rather than taken from glib or Foundation because this file is the half
// that has neither.
bool decodeBase64(const std::string &input, std::string *out) {
  static constexpr std::string_view kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::array<int8_t, 256> reverse{};
  reverse.fill(-1);
  for (size_t i = 0; i < kAlphabet.size(); i++) {
    reverse[static_cast<unsigned char>(kAlphabet[i])] = static_cast<int8_t>(i);
  }

  uint32_t accumulator = 0;
  int bits = 0;
  for (const char c : input) {
    if (c == '=' ) {
      break;
    }
    if (std::isspace(static_cast<unsigned char>(c)) != 0) {
      continue;
    }
    const int8_t value = reverse[static_cast<unsigned char>(c)];
    if (value < 0) {
      return false;
    }
    accumulator = (accumulator << 6) | static_cast<uint32_t>(value);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out->push_back(static_cast<char>((accumulator >> bits) & 0xFF));
    }
  }
  return true;
}

// Percent-decoding, for the path half of a file: URI. A path with a space in it
// arrives as %20 and opening it literally fails with "no such file", which is a
// confusing thing to be told about a file that plainly exists.
std::string percentDecode(const std::string &input) {
  std::string out;
  out.reserve(input.size());
  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == '%' && i + 2 < input.size()) {
      const auto hex = input.substr(i + 1, 2);
      char *end = nullptr;
      const long value = std::strtol(hex.c_str(), &end, 16);
      if (end != nullptr && *end == '\0') {
        out.push_back(static_cast<char>(value));
        i += 2;
        continue;
      }
    }
    out.push_back(input[i]);
  }
  return out;
}

bool readFile(const std::string &path, std::string *out, std::string *error) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    *error = "could not open " + path;
    return false;
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  *out = contents.str();
  if (out->empty()) {
    *error = "empty file: " + path;
    return false;
  }
  return true;
}

} // namespace

bool fetchImageBytes(const std::string &uri, std::string *out, std::string *error) {
  out->clear();

  if (uri.empty()) {
    *error = "empty source uri";
    return false;
  }

  if (startsWith(uri, "data:")) {
    const auto comma = uri.find(',');
    if (comma == std::string::npos) {
      *error = "malformed data: URI";
      return false;
    }
    if (uri.substr(0, comma).find(";base64") == std::string::npos) {
      // Percent-encoded text payloads are legal but never carry an image.
      *error = "only base64 data: URIs are supported";
      return false;
    }
    if (!decodeBase64(uri.substr(comma + 1), out)) {
      out->clear();
      *error = "malformed base64 in data: URI";
      return false;
    }
    return true;
  }

  if (startsWith(uri, "http://") || startsWith(uri, "https://")) {
    std::call_once(curlInitFlag, []() { curl_global_init(CURL_GLOBAL_DEFAULT); });

    CURL *handle = curl_easy_init();
    if (handle == nullptr) {
      *error = "could not initialise libcurl";
      return false;
    }
    curl_easy_setopt(handle, CURLOPT_URL, uri.c_str());
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, appendBytes);
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, out);
    // Without this, libcurl installs a SIGALRM-based resolver timeout that is
    // not safe to use off the main thread.
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    const CURLcode result = curl_easy_perform(handle);
    long status = 0;
    curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(handle);

    if (result != CURLE_OK) {
      out->clear();
      *error = curl_easy_strerror(result);
      return false;
    }
    if (status >= 400) {
      out->clear();
      *error = "http " + std::to_string(status);
      return false;
    }
    return true;
  }

  // Anything else is a path. file:// is the explicit form; a bare path is what
  // a bundled asset looks like once it has been resolved.
  std::string path = uri;
  if (startsWith(uri, "file://")) {
    path = percentDecode(uri.substr(std::string("file://").size()));
    // file:///tmp/x has an empty authority; file://host/x has one this cannot
    // reach, and treating its host as the first path segment would silently
    // read the wrong file.
    if (!path.empty() && path.front() != '/') {
      *error = "remote file: URIs are not supported: " + uri;
      return false;
    }

#ifdef _WIN32
    // On Windows the leading slash is not part of the path.
    //
    // `file:///C:/x` has the URI path `/C:/x`, and on a POSIX system that is
    // also the filesystem path -- which is why this went unnoticed until a
    // third desktop. Windows has no root above the drive letter, so `/C:/x`
    // does not open and the slash has to come off. Every browser and every URI
    // library does the same.
    //
    // Guarded rather than written portably, even though a POSIX directory
    // literally named `C:` is not a thing anyone has: this file is shared, the
    // other two desktops cannot be tested from here, and a shared behaviour
    // change is not worth making blind for a case that cannot arise.
    if (path.size() >= 3 && path[0] == '/' &&
        ((path[1] >= 'A' && path[1] <= 'Z') || (path[1] >= 'a' && path[1] <= 'z')) &&
        path[2] == ':') {
      path.erase(0, 1);
    }
#endif
  }

  if (!readFile(path, out, error)) {
    out->clear();
    return false;
  }
  return true;
}

} // namespace basalt
